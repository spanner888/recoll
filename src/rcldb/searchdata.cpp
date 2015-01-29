/* Copyright (C) 2006 J.F.Dockes
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

#include "autoconfig.h"

#include <stdio.h>

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
using namespace std;

#include "xapian.h"

#include "cstr.h"
#include "rcldb.h"
#include "rcldb_p.h"
#include "searchdata.h"
#include "debuglog.h"
#include "smallut.h"
#include "textsplit.h"
#include "unacpp.h"
#include "utf8iter.h"
#include "stoplist.h"
#include "rclconfig.h"
#include "termproc.h"
#include "synfamily.h"
#include "stemdb.h"
#include "expansiondbs.h"
#include "base64.h"
#include "daterange.h"

namespace Rcl {

typedef  vector<SearchDataClause *>::iterator qlist_it_t;
typedef  vector<SearchDataClause *>::const_iterator qlist_cit_t;

void SearchData::commoninit()
{
    m_haveDates = false;
    m_maxSize = size_t(-1);
    m_minSize = size_t(-1);
    m_haveWildCards = false;
    m_autodiacsens = false;
    m_autocasesens = true;
    m_maxexp = 10000;
    m_maxcl = 100000;
    m_softmaxexpand = -1;
}

SearchData::~SearchData() 
{
    LOGDEB0(("SearchData::~SearchData\n"));
    for (qlist_it_t it = m_query.begin(); it != m_query.end(); it++)
	delete *it;
}

// This is called by the GUI simple search if the option is set: add
// (OR) phrase to a query (if it is simple enough) so that results
// where the search terms are close and in order will come up on top.
// We remove very common terms from the query to avoid performance issues.
bool SearchData::maybeAddAutoPhrase(Rcl::Db& db, double freqThreshold)
{
    LOGDEB0(("SearchData::maybeAddAutoPhrase()\n"));
    if (!m_query.size()) {
	LOGDEB2(("SearchData::maybeAddAutoPhrase: empty query\n"));
	return false;
    }

    string field;
    vector<string> words;
    // Walk the clause list. If we find any non simple clause or different
    // field names, bail out.
    for (qlist_it_t it = m_query.begin(); it != m_query.end(); it++) {
	SClType tp = (*it)->m_tp;
	if (tp != SCLT_AND && tp != SCLT_OR) {
	    LOGDEB2(("SearchData::maybeAddAutoPhrase: rejected clause\n"));
	    return false;
	}
	SearchDataClauseSimple *clp = 
	    dynamic_cast<SearchDataClauseSimple*>(*it);
	if (clp == 0) {
	    LOGDEB2(("SearchData::maybeAddAutoPhrase: dyncast failed\n"));
	    return false;
	}
	if (it == m_query.begin()) {
	    field = clp->getfield();
	} else {
	    if (clp->getfield().compare(field)) {
		LOGDEB2(("SearchData::maybeAddAutoPhrase: diff. fields\n"));
		return false;
	    }
	}

	// If there are wildcards or quotes in there, bail out
	if (clp->gettext().find_first_of("\"*[?") != string::npos) { 
	    LOGDEB2(("SearchData::maybeAddAutoPhrase: wildcards\n"));
	    return false;
	}
        // Do a simple word-split here, don't bother with the full-blown
	// textsplit. The autophrase thing is just "best effort", it's
	// normal that it won't work in strange cases.
	vector<string> wl;
	stringToStrings(clp->gettext(), wl);
	words.insert(words.end(), wl.begin(), wl.end());
    }


    // Trim the word list by eliminating very frequent terms
    // (increasing the slack as we do it):
    int slack = 0;
    int doccnt = db.docCnt();
    if (!doccnt)
	doccnt = 1;
    string swords;
    for (vector<string>::iterator it = words.begin(); 
	 it != words.end(); it++) {
	double freq = double(db.termDocCnt(*it)) / doccnt;
	if (freq < freqThreshold) {
	    if (!swords.empty())
		swords.append(1, ' ');
	    swords += *it;
	} else {
	    LOGDEB0(("Autophrase: [%s] too frequent (%.2f %%)\n", 
		    it->c_str(), 100 * freq));
	    slack++;
	}
    }
    
    // We can't make a phrase with a single word :)
    int nwords = TextSplit::countWords(swords);
    if (nwords <= 1) {
	LOGDEB2(("SearchData::maybeAddAutoPhrase: ended with 1 word\n"));
	return false;
    }

    // Increase the slack: we want to be a little more laxist than for
    // an actual user-entered phrase
    slack += 1 + nwords / 3;
    
    m_autophrase = RefCntr<SearchDataClauseDist>(
	new SearchDataClauseDist(SCLT_PHRASE, swords, slack, field));
    return true;
}

// Add clause to current list. OR lists cant have EXCL clauses.
bool SearchData::addClause(SearchDataClause* cl)
{
    if (m_tp == SCLT_OR && cl->getexclude()) {
	LOGERR(("SearchData::addClause: cant add EXCL to OR list\n"));
	m_reason = "No Negative (AND_NOT) clauses allowed in OR queries";
	return false;
    }
    cl->setParent(this);
    m_haveWildCards = m_haveWildCards || cl->m_haveWildCards;
    m_query.push_back(cl);
    return true;
}

// Am I a file name only search ? This is to turn off term highlighting
bool SearchData::fileNameOnly() 
{
    for (qlist_it_t it = m_query.begin(); it != m_query.end(); it++)
	if (!(*it)->isFileName())
	    return false;
    return true;
}

// Extract all term data
void SearchData::getTerms(HighlightData &hld) const
{
    for (qlist_cit_t it = m_query.begin(); it != m_query.end(); it++)
	(*it)->getTerms(hld);
    return;
}

} // Namespace Rcl
