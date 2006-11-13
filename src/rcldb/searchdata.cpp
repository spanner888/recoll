#ifndef lint
static char rcsid[] = "@(#$Id: searchdata.cpp,v 1.1 2006-11-13 08:49:44 dockes Exp $ (C) 2006 J.F.Dockes";
#endif
/*
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

// Handle translation from rcl's SearchData structures to Xapian Queries

#include <string>
#include <list>
#ifndef NO_NAMESPACES
using namespace std;
#endif

#include "xapian.h"

#include "rcldb.h"
#include "searchdata.h"
#include "debuglog.h"
#include "smallut.h"
#include "textsplit.h"
#include "unacpp.h"
#include "utf8iter.h"

namespace Rcl {

typedef  list<SearchDataClause *>::iterator qlist_it_t;

bool SearchData::toNativeQuery(Rcl::Db &db, void *d, const string& stemlang)
{
    Xapian::Query xq;

    // Walk the clause list translating each in turn and building the 
    // Xapian query tree
    for (qlist_it_t it = m_query.begin(); it != m_query.end(); it++) {
	Xapian::Query nq;
	(*it)->toNativeQuery(db, &nq, stemlang);
	Xapian::Query::op op;

	// If this structure is an AND list, must use AND_NOT for excl clauses.
	// Else this is an OR list, and there can't be excl clauses
	if (m_tp == SCLT_AND) {
	    op = (*it)->m_tp == SCLT_EXCL ? 
		Xapian::Query::OP_AND_NOT: Xapian::Query::OP_AND;
	} else {
	    op = Xapian::Query::OP_OR;
	}
	xq = xq.empty() ? nq : Xapian::Query(op, xq, nq);
    }

    // Add the file type filtering clause if any
    if (!m_filetypes.empty()) {
	list<Xapian::Query> pqueries;
	Xapian::Query tq;
	for (list<string>::iterator it = m_filetypes.begin(); 
	     it != m_filetypes.end(); it++) {
	    string term = "T" + *it;
	    LOGDEB(("Adding file type term: [%s]\n", term.c_str()));
	    tq = tq.empty() ? Xapian::Query(term) : 
		Xapian::Query(Xapian::Query::OP_OR, tq, Xapian::Query(term));
	}
	xq = xq.empty() ? tq : Xapian::Query(Xapian::Query::OP_FILTER, xq, tq);
    }

    *((Xapian::Query *)d) = xq;
    return true;
}

// Add clause to current list. OR lists cant have EXCL clauses.
bool SearchData::addClause(SearchDataClause* cl)
{
    if (m_tp == SCLT_OR && (cl->m_tp == SCLT_EXCL)) {
	LOGERR(("SearchData::addClause: cant add EXCL to OR list\n"));
	return false;
    }
    m_query.push_back(cl);
    return true;
}

// Make me all new
void SearchData::erase() {
    for (qlist_it_t it = m_query.begin(); it != m_query.end(); it++)
	delete *it;
    m_query.clear();
    m_filetypes.clear();
    m_topdir.erase();
    m_description.erase();
}

// Am I a file name only search ? This is to turn off term highlighting
bool SearchData::fileNameOnly() {
    for (qlist_it_t it = m_query.begin(); it != m_query.end(); it++)
	if (!(*it)->isFileName())
	    return false;
    return true;
}

// Splitter callback for breaking a user query string into simple
// terms and phrases
class wsQData : public TextSplitCB {
 public:
    vector<string> terms;
    // Debug
    string catterms() {
	string s;
	for (unsigned int i = 0; i < terms.size(); i++) {
	    s += "[" + terms[i] + "] ";
	}
	return s;
    }
    bool takeword(const std::string &term, int , int, int) {
	LOGDEB1(("wsQData::takeword: %s\n", term.c_str()));
	terms.push_back(term);
	return true;
    }
    // Decapital + deaccent all terms 
    void dumball() {
	for (vector<string>::iterator it=terms.begin(); it !=terms.end();it++){
	    string dumb;
	    dumb_string(*it, dumb);
	    *it = dumb;
	}
    }
};


// Turn string into list of xapian queries. There is little
// interpretation done on the string (no +term -term or filename:term
// stuff). We just separate words and phrases, and interpret
// capitalized terms as wanting no stem expansion. 
// The final list contains one query for each term or phrase
//   - Elements corresponding to a stem-expanded part are an OP_OR
//     composition of the stem-expanded terms (or a single term query).
//   - Elements corresponding to a phrase are an OP_PHRASE composition of the
//     phrase terms (no stem expansion in this case)
static void stringToXapianQueries(const string &iq,
				  const string& stemlang,
				  Db& db,
				  list<Xapian::Query> &pqueries)
{
    string qstring = iq;
    bool opt_stemexp = !stemlang.empty();

    // Split into (possibly single word) phrases ("this is a phrase"):
    list<string> phrases;
    stringToStrings(qstring, phrases);

    // Then process each phrase: split into terms and transform into
    // appropriate Xapian Query

    for (list<string>::iterator it=phrases.begin(); it !=phrases.end(); it++) {
	LOGDEB(("strToXapianQ: phrase or word: [%s]\n", it->c_str()));

	// If there are both spans and single words in this element,
	// we need to use a word split, else a phrase query including
	// a span would fail if we didn't adjust the proximity to
	// account for the additional span term which is complicated.
	wsQData splitDataS, splitDataW;
	TextSplit splitterS(&splitDataS, TextSplit::TXTS_ONLYSPANS);
	splitterS.text_to_words(*it);
	TextSplit splitterW(&splitDataW, TextSplit::TXTS_NOSPANS);
	splitterW.text_to_words(*it);
	wsQData& splitData = splitDataS;
	if (splitDataS.terms.size() > 1 && splitDataS.terms.size() != 
	    splitDataW.terms.size())
	    splitData = splitDataW;

	LOGDEB1(("strToXapianQ: splitter term count: %d\n", 
		splitData.terms.size()));
	switch(splitData.terms.size()) {
	case 0: continue;// ??
	case 1: // Not a real phrase: one term
	    {
		string term = splitData.terms.front();
		bool nostemexp = false;
		// Check if the first letter is a majuscule in which
		// case we do not want to do stem expansion. Note that
		// the test is convoluted and possibly problematic
		if (term.length() > 0) {
		    string noacterm,noaclowterm;
		    if (unacmaybefold(term, noacterm, "UTF-8", false) &&
			unacmaybefold(noacterm, noaclowterm, "UTF-8", true)) {
			Utf8Iter it1(noacterm);
			Utf8Iter it2(noaclowterm);
			if (*it1 != *it2)
			    nostemexp = true;
		    }
		}
		LOGDEB1(("Term: %s stem expansion: %s\n", 
			term.c_str(), nostemexp?"no":"yes"));

		list<string> exp;  
		string term1;
		dumb_string(term, term1);
		// Possibly perform stem compression/expansion
		if (!nostemexp && opt_stemexp) {
		    exp = db.stemExpand(stemlang, term1);
		} else {
		    exp.push_back(term1);
		}

		// Push either term or OR of stem-expanded set
		pqueries.push_back(Xapian::Query(Xapian::Query::OP_OR, 
						 exp.begin(), exp.end()));
	    }
	    break;

	default:
	    // Phrase: no stem expansion
	    splitData.dumball();
	    LOGDEB(("Pushing phrase: [%s]\n", splitData.catterms().c_str()));
	    pqueries.push_back(Xapian::Query(Xapian::Query::OP_PHRASE,
					     splitData.terms.begin(),
					     splitData.terms.end()));
	}
    }
}

// Translate a simple OR, AND, or EXCL search clause. 
bool SearchDataClauseSimple::toNativeQuery(Rcl::Db &db, void *p, 
					   const string& stemlang)
{
    Xapian::Query *qp = (Xapian::Query *)p;
    *qp = Xapian::Query();

    Xapian::Query::op op;
    switch (m_tp) {
    case SCLT_AND: op = Xapian::Query::OP_AND; break;
    case SCLT_OR: 
    case SCLT_EXCL: op = Xapian::Query::OP_OR; break;
    default:
	LOGERR(("SearchDataClauseSimple: bad m_tp %d\n", m_tp));
	return false;
    }
    list<Xapian::Query> pqueries;
    stringToXapianQueries(m_text, stemlang, db, pqueries);
    if (pqueries.empty()) {
	LOGERR(("SearchDataClauseSimple: resolved to null query\n"));
	return true;
    }
    *qp = Xapian::Query(op, pqueries.begin(), pqueries.end());
    return true;
}

// Translate a FILENAME search clause. 
bool SearchDataClauseFilename::toNativeQuery(Rcl::Db &db, void *p, 
					     const string& stemlang)
{
    Xapian::Query *qp = (Xapian::Query *)p;
    *qp = Xapian::Query();

    list<string> names;
    db.filenameWildExp(m_text, names);
    // Build a query out of the matching file name terms.
    *qp = Xapian::Query(Xapian::Query::OP_OR, names.begin(), names.end());
    return true;
}

// Translate NEAR or PHRASE clause. We're not handling the distance parameter
// yet.
bool SearchDataClauseDist::toNativeQuery(Rcl::Db &db, void *p, 
					 const string& stemlang)
{
    Xapian::Query *qp = (Xapian::Query *)p;
    *qp = Xapian::Query();
    
    Xapian::Query::op op = m_tp == SCLT_PHRASE ? Xapian::Query::OP_PHRASE :
	Xapian::Query::OP_NEAR;

    list<Xapian::Query> pqueries;
    Xapian::Query nq;
    string s = string("\"") + m_text + string("\"");

    // Use stringToXapianQueries anyway to lowercase and simplify the
    // phrase terms etc. The result should be a single element list
    stringToXapianQueries(s, stemlang, db, pqueries);
    if (pqueries.empty()) {
	LOGERR(("SearchDataClauseDist: resolved to null query\n"));
	return true;
    }
    *qp = *pqueries.begin();
    return true;
}

} // Namespace Rcl
