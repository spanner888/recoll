/* Copyright (C) 2005 J.F.Dockes 
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


#include "autoconfig.h"

#include "debuglog.h"
#include "utf8iter.h"
#include "smallut.h"
#include "refcntr.h"
#include "textsplit.h"
#include "xmacros.h"
#include "rcldb.h"
#include "stemdb.h"
#include "expansiondbs.h"

using namespace std;

namespace Rcl {

/**
 * Create all expansion dbs used to transform user input term to widen a query
 * We use Xapian synonyms subsets to store the expansions.
 */
bool createExpansionDbs(Xapian::WritableDatabase& wdb, 
			const vector<string>& langs)
{
    LOGDEB(("StemDb::createExpansionDbs: languages: %s\n", 
	    stringsToString(langs).c_str()));
    Chrono cron;

    // Erase and recreate all the expansion groups

    // Stem dbs
    vector<XapWritableComputableSynFamMember> stemdbs;
    // Note: tried to make this to work with stack-allocated objects, couldn't.
    // Looks like a bug in copy constructors somewhere, can't guess where
    vector<RefCntr<SynTermTransStem> > stemmers;
    for (unsigned int i = 0; i < langs.size(); i++) {
	stemmers.push_back(RefCntr<SynTermTransStem>
			   (new SynTermTransStem(langs[i])));
	stemdbs.push_back(
	    XapWritableComputableSynFamMember(wdb, synFamStem, langs[i], 
					      stemmers.back().getptr()));
	stemdbs.back().recreate();
    }

#ifndef RCL_INDEX_STRIPCHARS
    // Unaccented stem dbs
    vector<XapWritableComputableSynFamMember> unacstemdbs;
    // We can reuse the same stemmer pointers, the objects are stateless.
    for (unsigned int i = 0; i < langs.size(); i++) {
	unacstemdbs.push_back(
	    XapWritableComputableSynFamMember(wdb, synFamStemUnac, langs[i], 
					      stemmers.back().getptr()));
	unacstemdbs.back().recreate();
    }

    SynTermTransUnac transunac(UNACOP_UNACFOLD);
    XapWritableComputableSynFamMember 
	diacasedb(wdb, synFamDiac, "all", &transunac);
    diacasedb.recreate();
#endif

    // Walk the list of all terms, and stem/unac each.
    string ermsg;
    try {
        for (Xapian::TermIterator it = wdb.allterms_begin(); 
	     it != wdb.allterms_end(); it++) {

	    // Skip terms which don't look like natural language words.
            if (!Db::isSpellingCandidate(*it)) {
                LOGDEB1(("createExpansionDbs: skipped: [%s]\n", (*it).c_str()));
                continue;
            }

	    // Detect and skip CJK terms.
	    // We're still sending all other multibyte utf-8 chars to
            // the stemmer, which is not too well defined for
            // xapian<1.0 (very obsolete now), but seems to work
            // anyway. There shouldn't be too many in any case because
            // accents are stripped at this point. 
	    // The effect of stripping accents on stemming is not good, 
            // (e.g: in french partimes -> partim, parti^mes -> part)
	    // but fixing the issue would be complicated.
	    Utf8Iter utfit(*it);
	    if (TextSplit::isCJK(*utfit)) {
		// LOGDEB(("stemskipped: Skipping CJK\n"));
		continue;
	    }

	    string lower = *it;
#ifndef RCL_INDEX_STRIPCHARS
	    // If the index is raw, compute the case-folded term which
	    // is the input to the stem db, and add a synonym from the
	    // stripped term to the cased and accented one, for accent
	    // and case expansion at query time
	    unacmaybefold(*it, lower, "UTF-8", UNACOP_FOLD);
	    diacasedb.addSynonym(*it);
#endif

	    // Create stemming synonym for every language. The input is the 
	    // lowercase accented term
	    for (unsigned int i = 0; i < langs.size(); i++) {
		stemdbs[i].addSynonym(lower);
	    }

#ifndef RCL_INDEX_STRIPCHARS
	    // For a raw index, also maybe create a stem expansion for
	    // the unaccented term. While this may be incorrect, it is
	    // also necessary for searching in a diacritic-unsensitive
	    // way on a raw index
	    string unac;
	    unacmaybefold(lower, unac, "UTF-8", UNACOP_UNAC);
	    if (unac != lower)
		for (unsigned int i = 0; i < langs.size(); i++) {
		    unacstemdbs[i].addSynonym(unac);
		}
#endif
        }
    } XCATCHERROR(ermsg);
    if (!ermsg.empty()) {
        LOGERR(("Db::createStemDb: map build failed: %s\n", ermsg.c_str()));
        return false;
    }

    LOGDEB(("StemDb::createExpansionDbs: done: %.2f S\n", cron.secs()));
    return true;
}

}    