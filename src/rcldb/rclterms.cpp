/* Copyright (C) 2004 J.F.Dockes
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

////////////////////////////////////////////////////////////////////
/** Things dealing with walking the terms lists and expansion dbs */

#include "autoconfig.h"

#include <string>
using namespace std;

#include "debuglog.h"
#include "rcldb.h"
#include "rcldb_p.h"
#include "stemdb.h"
#include "expansiondbs.h"

namespace Rcl {

// File name wild card expansion. This is a specialisation ot termMatch
bool Db::filenameWildExp(const string& fnexp, vector<string>& names, int max)
{
    string pattern = fnexp;
    names.clear();

    // If pattern is not capitalized, not quoted (quoted pattern can't
    // get here currently anyway), and has no wildcards, we add * at
    // each end: match any substring
    if (pattern[0] == '"' && pattern[pattern.size()-1] == '"') {
	pattern = pattern.substr(1, pattern.size() -2);
    } else if (pattern.find_first_of(cstr_minwilds) == string::npos && 
	       !unaciscapital(pattern)) {
	pattern = "*" + pattern + "*";
    } // else let it be

    LOGDEB(("Rcl::Db::filenameWildExp: pattern: [%s]\n", pattern.c_str()));

    // We inconditionnally lowercase and strip the pattern, as is done
    // during indexing. This seems to be the only sane possible
    // approach with file names and wild cards. termMatch does
    // stripping conditionally on indexstripchars.
    string pat1;
    if (unacmaybefold(pattern, pat1, "UTF-8", UNACOP_UNACFOLD)) {
	pattern.swap(pat1);
    }

    TermMatchResult result;
    if (!termMatch(ET_WILD, string(), pattern, result, max,
		   unsplitFilenameFieldName))
	return false;
    for (vector<TermMatchEntry>::const_iterator it = result.entries.begin();
	 it != result.entries.end(); it++) 
	names.push_back(it->term);

    if (names.empty()) {
	// Build an impossible query: we know its impossible because we
	// control the prefixes!
	names.push_back(wrap_prefix("XNONE") + "NoMatchingTerms");
    }
    return true;
}

// Walk the Y terms and return min/max
bool Db::maxYearSpan(int *minyear, int *maxyear)
{
    LOGDEB(("Rcl::Db:maxYearSpan\n"));
    *minyear = 1000000; 
    *maxyear = -1000000;
    TermMatchResult result;
    if (!termMatch(ET_WILD, string(), "*", result, -1, "xapyear")) {
	LOGINFO(("Rcl::Db:maxYearSpan: termMatch failed\n"));
	return false;
    }
    for (vector<TermMatchEntry>::const_iterator it = result.entries.begin();
	 it != result.entries.end(); it++) {
        if (!it->term.empty()) {
            int year = atoi(strip_prefix(it->term).c_str());
            if (year < *minyear)
                *minyear = year;
            if (year > *maxyear)
                *maxyear = year;
        }
    }
    return true;
}

class TermMatchCmpByWcf {
public:
    int operator()(const TermMatchEntry& l, const TermMatchEntry& r) {
	return r.wcf - l.wcf < 0;
    }
};
class TermMatchCmpByTerm {
public:
    int operator()(const TermMatchEntry& l, const TermMatchEntry& r) {
	return l.term.compare(r.term) > 0;
    }
};
class TermMatchTermEqual {
public:
    int operator()(const TermMatchEntry& l, const TermMatchEntry& r) {
	return !l.term.compare(r.term);
    }
};

/** Add prefix to all strings in list. 
 * @param prefix already wrapped prefix
 */
static void addPrefix(vector<TermMatchEntry>& terms, const string& prefix)
{
    if (prefix.empty())
	return;
    for (vector<TermMatchEntry>::iterator it = terms.begin(); 
         it != terms.end(); it++)
	it->term.insert(0, prefix);
}

// Find all index terms that match an input along different expansion modes:
// wildcard, regular expression, or stemming. Depending on flags we perform
// case and/or diacritics expansion (this can be the only thing requested).
// If the "field" parameter is set, we return a list of appropriately
// prefixed terms (which are going to be used to build a Xapian
// query). 
// This routine performs case/diacritics/stemming expansion against
// the auxiliary tables, and possibly calls idxTermMatch() for work
// using the main index terms (filtering, retrieving stats, expansion
// in some cases).
bool Db::termMatch(int typ_sens, const string &lang, const string &_term,
		   TermMatchResult& res, int max,  const string& field)
{
    int matchtyp = matchTypeTp(typ_sens);
    if (!m_ndb || !m_ndb->m_isopen)
	return false;
    Xapian::Database xrdb = m_ndb->xrdb;

    bool diac_sensitive = (typ_sens & ET_DIACSENS) != 0;
    bool case_sensitive = (typ_sens & ET_CASESENS) != 0;

    bool stripped = false;
#ifdef RCL_INDEX_STRIPCHARS
    stripped = true;
#else
    stripped = o_index_stripchars;
#endif

    LOGDEB(("Db::TermMatch: typ %d diacsens %d casesens %d lang [%s] term [%s] "
	    "max %d field [%s] stripped %d\n",
	    matchtyp, diac_sensitive, case_sensitive, lang.c_str(), 
	    _term.c_str(), max, field.c_str(), stripped));

    // If index is stripped, no case or diac expansion can be needed:
    // for the processing inside this routine, everything looks like
    // we're all-sensitive: no use of expansion db.
    // Also, convert input to lowercase and strip its accents.
    string term = _term;
    if (stripped) {
	diac_sensitive = case_sensitive = true;
	if (!unacmaybefold(_term, term, "UTF-8", UNACOP_UNACFOLD)) {
	    LOGERR(("Db::termMatch: unac failed for [%s]\n", _term.c_str()));
	    return false;
	}
    }

#ifndef RCL_INDEX_STRIPCHARS
    // The case/diac expansion db
    SynTermTransUnac unacfoldtrans(UNACOP_UNACFOLD);
    XapComputableSynFamMember synac(xrdb, synFamDiCa, "all", &unacfoldtrans);
#endif // RCL_INDEX_STRIPCHARS


    if (matchtyp == ET_WILD || matchtyp == ET_REGEXP) {
#ifdef RCL_INDEX_STRIPCHARS
	idxTermMatch(typ_sens, lang, term, res, max, field);
#else
	RefCntr<StrMatcher> matcher;
	if (matchtyp == ET_WILD) {
	    matcher = RefCntr<StrMatcher>(new StrWildMatcher(term));
	} else {
	    matcher = RefCntr<StrMatcher>(new StrRegexpMatcher(term));
	}
	if (!diac_sensitive || !case_sensitive) {
	    // Perform case/diac expansion on the exp as appropriate and
	    // expand the result.
	    vector<string> exp;
	    if (diac_sensitive) {
		// Expand for diacritics and case, filtering for same diacritics
		SynTermTransUnac foldtrans(UNACOP_FOLD);
		synac.synKeyExpand(matcher.getptr(), exp, &foldtrans);
	    } else if (case_sensitive) {
		// Expand for diacritics and case, filtering for same case
		SynTermTransUnac unactrans(UNACOP_UNAC);
		synac.synKeyExpand(matcher.getptr(), exp, &unactrans);
	    } else {
		// Expand for diacritics and case, no filtering
		synac.synKeyExpand(matcher.getptr(), exp);
	    }
	    // Retrieve additional info and filter against the index itself
	    for (vector<string>::const_iterator it = exp.begin(); 
		 it != exp.end(); it++) {
		idxTermMatch(ET_NONE, "", *it, res, max, field);
	    }
	} else {
	    idxTermMatch(typ_sens, lang, term, res, max, field);
	}

#endif // RCL_INDEX_STRIPCHARS

    } else {
	// Expansion is STEM or NONE (which may still need case/diac exp)

#ifdef RCL_INDEX_STRIPCHARS

	idxTermMatch(Rcl::Db::ET_STEM, lang, term, res, max, field);

#else
	vector<string> lexp;
	if (diac_sensitive && case_sensitive) {
	    // No case/diac expansion
	    lexp.push_back(term);
	} else if (diac_sensitive) {
	    // Expand for accents and case, filtering for same accents,
	    SynTermTransUnac foldtrans(UNACOP_FOLD);
	    synac.synExpand(term, lexp, &foldtrans);
	} else if (case_sensitive) {
	    // Expand for accents and case, filtering for same case
	    SynTermTransUnac unactrans(UNACOP_UNAC);
	    synac.synExpand(term, lexp, &unactrans);
	} else {
	    // We are neither accent- nor case- sensitive and may need stem
	    // expansion or not. Expand for accents and case
	    synac.synExpand(term, lexp);
	}

	if (matchTypeTp(typ_sens) == ET_STEM) {
	    // Need stem expansion. Lowercase the result of accent and case
	    // expansion for input to stemdb.
	    for (unsigned int i = 0; i < lexp.size(); i++) {
		string lower;
		unacmaybefold(lexp[i], lower, "UTF-8", UNACOP_FOLD);
		lexp[i] = lower;
	    }
	    sort(lexp.begin(), lexp.end());
	    lexp.erase(unique(lexp.begin(), lexp.end()), lexp.end());
	    StemDb sdb(xrdb);
	    vector<string> exp1;
	    for (vector<string>::const_iterator it = lexp.begin(); 
		 it != lexp.end(); it++) {
		sdb.stemExpand(lang, *it, exp1);
	    }
	    LOGDEB(("ExpTerm: stem exp-> %s\n", stringsToString(exp1).c_str()));

	    // Expand the resulting list for case (all stemdb content
	    // is lowercase)
	    lexp.clear();
	    for (vector<string>::const_iterator it = exp1.begin(); 
		 it != exp1.end(); it++) {
		synac.synExpand(*it, lexp);
	    }
	    sort(lexp.begin(), lexp.end());
	    lexp.erase(unique(lexp.begin(), lexp.end()), lexp.end());
	}

	// Filter the result and get the stats, possibly add prefixes.
	LOGDEB(("ExpandTerm:TM: lexp: %s\n", stringsToString(lexp).c_str()));
	for (vector<string>::const_iterator it = lexp.begin();
	     it != lexp.end(); it++) {
	    idxTermMatch(Rcl::Db::ET_WILD, "", *it, res, max, field);
	}
    }
#endif

    TermMatchCmpByTerm tcmp;
    sort(res.entries.begin(), res.entries.end(), tcmp);
    TermMatchTermEqual teq;
    vector<TermMatchEntry>::iterator uit = 
	unique(res.entries.begin(), res.entries.end(), teq);
    res.entries.resize(uit - res.entries.begin());
    TermMatchCmpByWcf wcmp;
    sort(res.entries.begin(), res.entries.end(), wcmp);
    if (max > 0) {
	// Would need a small max and big stem expansion...
	res.entries.resize(MIN(res.entries.size(), (unsigned int)max));
    }
    return true;
}

// Second phase of wildcard/regexp term expansion after case/diac
// expansion: expand against main index terms
bool Db::idxTermMatch(int typ_sens, const string &lang, const string &root,
		      TermMatchResult& res, int max,  const string& field)
{
    int typ = matchTypeTp(typ_sens);

#ifndef RCL_INDEX_STRIPCHARS
    if (typ == ET_STEM) {
	LOGFATAL(("RCLDB: internal error: idxTermMatch called with ET_STEM\n"));
	abort();
    }
#endif

    Xapian::Database xdb = m_ndb->xrdb;

    string prefix;
    if (!field.empty()) {
	const FieldTraits *ftp = 0;
	if (!fieldToTraits(field, &ftp) || ftp->pfx.empty()) {
            LOGDEB(("Db::termMatch: field is not indexed (no prefix): [%s]\n", 
                    field.c_str()));
        } else {
	    prefix = wrap_prefix(ftp->pfx);
	}
    }
    res.prefix = prefix;

#ifdef RCL_INDEX_STRIPCHARS
    if (typ == ET_STEM) {
	vector<string> exp;
	StemDb db(m_ndb->xrdb);
	if (!db.stemExpand(langs, term, exp))
	    return false;
	res.entries.insert(result.entries.end(), exp.begin(), exp.end());
	for (vector<TermMatchEntry>::iterator it = res.entries.begin(); 
	     it != res.entries.end(); it++) {
	    XAPTRY(it->wcf = xdb.get_collection_freq(it->term);
                   it->docs = xdb.get_termfreq(it->term),
                   xdb, m_reason);
            if (!m_reason.empty())
                return false;
	    LOGDEB1(("termMatch: %d [%s]\n", it->wcf, it->term.c_str()));
	}
        if (!prefix.empty())
            addPrefix(res.entries, prefix);
    } else 
#endif
    {
	RefCntr<StrMatcher> matcher;
	if (typ == ET_REGEXP) {
	    matcher = RefCntr<StrMatcher>(new StrRegexpMatcher(root));
	    if (!matcher->ok()) {
		LOGERR(("termMatch: regcomp failed: %s\n", 
			matcher->getreason().c_str()))
		    return false;
	    }
	} else if (typ == ET_WILD) {
	    matcher = RefCntr<StrMatcher>(new StrWildMatcher(root));
	}

	// Find the initial section before any special char
	string::size_type es = string::npos;
	if (matcher.isNotNull()) {
	    es = matcher->baseprefixlen();
	}
	string is;
	switch (es) {
	case string::npos: is = prefix + root; break;
	case 0: is = prefix; break;
	default: is = prefix + root.substr(0, es); break;
	}
	LOGDEB2(("termMatch: initsec: [%s]\n", is.c_str()));

        for (int tries = 0; tries < 2; tries++) { 
            try {
                Xapian::TermIterator it = xdb.allterms_begin(); 
                if (!is.empty())
                    it.skip_to(is.c_str());
                for (int rcnt = 0; it != xdb.allterms_end(); it++) {
                    // If we're beyond the terms matching the initial
                    // string, end
                    if (!is.empty() && (*it).find(is) != 0)
                        break;
                    string term;
                    if (!prefix.empty())
                        term = (*it).substr(prefix.length());
                    else
                        term = *it;

		    if (matcher.isNotNull() && !matcher->match(term))
			continue;

                    res.entries.push_back(
			TermMatchEntry(*it, xdb.get_collection_freq(*it),
				       it.get_termfreq()));

		    // The problem with truncating here is that this is done
		    // alphabetically and we may not keep the most frequent 
		    // terms. OTOH, not doing it may stall the program if
		    // we are walking the whole term list. We compromise
		    // by cutting at 2*max
                    if (max > 0 && ++rcnt >= 2*max)
			break;
                }
                m_reason.erase();
                break;
            } catch (const Xapian::DatabaseModifiedError &e) {
                m_reason = e.get_msg();
                xdb.reopen();
                continue;
            } XCATCHERROR(m_reason);
            break;
        }
	if (!m_reason.empty()) {
	    LOGERR(("termMatch: %s\n", m_reason.c_str()));
	    return false;
	}
    }

    return true;
}

/** Term list walking. */
class TermIter {
public:
    Xapian::TermIterator it;
    Xapian::Database db;
};
TermIter *Db::termWalkOpen()
{
    if (!m_ndb || !m_ndb->m_isopen)
	return 0;
    TermIter *tit = new TermIter;
    if (tit) {
	tit->db = m_ndb->xrdb;
        XAPTRY(tit->it = tit->db.allterms_begin(), tit->db, m_reason);
	if (!m_reason.empty()) {
	    LOGERR(("Db::termWalkOpen: xapian error: %s\n", m_reason.c_str()));
	    return 0;
	}
    }
    return tit;
}
bool Db::termWalkNext(TermIter *tit, string &term)
{
    XAPTRY(
	if (tit && tit->it != tit->db.allterms_end()) {
	    term = *(tit->it)++;
	    return true;
	}
        , tit->db, m_reason);

    if (!m_reason.empty()) {
	LOGERR(("Db::termWalkOpen: xapian error: %s\n", m_reason.c_str()));
    }
    return false;
}
void Db::termWalkClose(TermIter *tit)
{
    try {
	delete tit;
    } catch (...) {}
}

bool Db::termExists(const string& word)
{
    if (!m_ndb || !m_ndb->m_isopen)
	return 0;

    XAPTRY(if (!m_ndb->xrdb.term_exists(word)) return false,
           m_ndb->xrdb, m_reason);

    if (!m_reason.empty()) {
	LOGERR(("Db::termWalkOpen: xapian error: %s\n", m_reason.c_str()));
	return false;
    }
    return true;
}

bool Db::stemDiffers(const string& lang, const string& word, 
		     const string& base)
{
    Xapian::Stem stemmer(lang);
    if (!stemmer(word).compare(stemmer(base))) {
	LOGDEB2(("Rcl::Db::stemDiffers: same for %s and %s\n", 
		word.c_str(), base.c_str()));
	return false;
    }
    return true;
}

} // End namespace Rcl
