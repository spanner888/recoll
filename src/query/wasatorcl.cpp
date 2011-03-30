#ifndef lint
static char rcsid[] = "@(#$Id: wasatorcl.cpp,v 1.18 2008-12-05 11:09:31 dockes Exp $ (C) 2006 J.F.Dockes";
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
#include <cstdio>
#include <string>
#include <list>
#include <algorithm>
using std::string;
using std::list;

#include "rclconfig.h"
#include "wasastringtoquery.h"
#include "rcldb.h"
#include "searchdata.h"
#include "wasatorcl.h"
#include "debuglog.h"
#include "smallut.h"
#include "rclconfig.h"
#include "refcntr.h"
#include "textsplit.h"

static Rcl::SearchData *wasaQueryToRcl(RclConfig *config, WasaQuery *wasa, 
				       const string& autosuffs, string& reason)
{
    if (wasa == 0) {
	reason = "NULL query";
	return 0;
    }
    if (wasa->m_op != WasaQuery::OP_AND && wasa->m_op != WasaQuery::OP_OR) {
	reason = "Top query neither AND nor OR ?";
	LOGERR(("wasaQueryToRcl: top query neither AND nor OR!\n"));
	return 0;
    }

    Rcl::SearchData *sdata = new 
	Rcl::SearchData(wasa->m_op == WasaQuery::OP_AND ? Rcl::SCLT_AND : 
			Rcl::SCLT_OR);
    LOGDEB2(("wasaQueryToRcl: %s chain\n", wasa->m_op == WasaQuery::OP_AND ? 
	     "AND" : "OR"));

    WasaQuery::subqlist_t::iterator it;
    Rcl::SearchDataClause *nclause;

    // Walk the list of clauses. Some pseudo-field types need special
    // processing, which results in setting data in the top struct
    // instead of adding a clause. We check for these first
    for (it = wasa->m_subs.begin(); it != wasa->m_subs.end(); it++) {

	if (!stringicmp("mime", (*it)->m_fieldspec) ||
	    !stringicmp("format", (*it)->m_fieldspec)) {
	    if ((*it)->m_op != WasaQuery::OP_LEAF) {
		reason = "Negative mime/format clauses not supported yet";
		return 0;
	    }
	    sdata->addFiletype((*it)->m_value);
	    continue;
	} 

	// Xesam uses "type", we also support "rclcat", for broad
	// categories like "audio", "presentation", etc.
	if (!stringicmp("rclcat", (*it)->m_fieldspec) ||
	    !stringicmp("type", (*it)->m_fieldspec)) {
	    if ((*it)->m_op != WasaQuery::OP_LEAF) {
		reason = "Negative rclcat/type clauses not supported yet";
		return 0;
	    }
	    list<string> mtypes;
	    if (config && config->getMimeCatTypes((*it)->m_value, mtypes)
		&& !mtypes.empty()) {
		for (list<string>::iterator mit = mtypes.begin();
		     mit != mtypes.end(); mit++) {
		    sdata->addFiletype(*mit);
		}
	    } else {
		reason = "Unknown rclcat/type value: no mime types found";
		return 0;
	    }
	    continue;
	}

	// Filtering on location
	if (!stringicmp("dir", (*it)->m_fieldspec)) {
	    sdata->setTopdir((*it)->m_value, (*it)->m_op == WasaQuery::OP_EXCL);
	    continue;
	} 

	// Handle "date" spec
	if (!stringicmp("date", (*it)->m_fieldspec)) {
	    if ((*it)->m_op != WasaQuery::OP_LEAF) {
		reason = "Negative date filtering not supported";
		return 0;
	    }
	    DateInterval di;
	    if (!parsedateinterval((*it)->m_value, &di)) {
		LOGERR(("wasaQueryToRcl: bad date interval format\n"));
		reason = "Bad date interval format";
		return 0;
	    }
	    LOGDEB(("wasaQueryToRcl:: date span:  %d-%d-%d/%d-%d-%d\n",
		    di.y1,di.m1,di.d1, di.y2,di.m2,di.d2));
	    sdata->setDateSpan(&di);
	    continue;
	} 

	// "Regular" processing follows:
	switch ((*it)->m_op) {
	case WasaQuery::OP_NULL:
	case WasaQuery::OP_AND:
	default:
	    reason = "Found bad NULL or AND query type in list";
	    LOGERR(("wasaQueryToRcl: found bad NULL or AND q type in list\n"));
	    continue;

	case WasaQuery::OP_LEAF: {
	    LOGDEB2(("wasaQueryToRcl: leaf clause [%s]:[%s]\n", 
		     (*it)->m_fieldspec.c_str(), (*it)->m_value.c_str()));

            // Change terms found in the "autosuffs" list into "ext"
            // field queries
            if ((*it)->m_fieldspec.empty() && !autosuffs.empty()) {
                vector<string> asfv;
                if (stringToStrings(autosuffs, asfv)) {
                    if (find_if(asfv.begin(), asfv.end(), 
                                StringIcmpPred((*it)->m_value)) != asfv.end()) {
                        (*it)->m_fieldspec = "ext";
                        (*it)->m_modifiers |= WasaQuery::WQM_NOSTEM;
                    }
                }
            }

	    unsigned int mods = (unsigned int)(*it)->m_modifiers;

	    if (TextSplit::hasVisibleWhite((*it)->m_value)) {
		int slack = (mods & WasaQuery::WQM_PHRASESLACK) ? 10 : 0;
		Rcl::SClType tp = Rcl::SCLT_PHRASE;
		if (mods & WasaQuery::WQM_PROX) {
		    tp = Rcl::SCLT_NEAR;
		    slack = 10;
		}
		nclause = new Rcl::SearchDataClauseDist(tp, (*it)->m_value,
							slack,
							(*it)->m_fieldspec);
	    } else {
		nclause = new Rcl::SearchDataClauseSimple(Rcl::SCLT_AND, 
							  (*it)->m_value, 
							  (*it)->m_fieldspec);
	    }
	    if (nclause == 0) {
		reason = "Out of memory";
		LOGERR(("wasaQueryToRcl: out of memory\n"));
		return 0;
	    }
	    if (mods & WasaQuery::WQM_NOSTEM) {
		nclause->setModifiers(Rcl::SearchDataClause::SDCM_NOSTEMMING);
	    }
	    sdata->addClause(nclause);
	}
	    break;
	    
	case WasaQuery::OP_EXCL:
	    LOGDEB2(("wasaQueryToRcl: excl clause [%s]:[%s]\n", 
                    (*it)->m_fieldspec.c_str(), (*it)->m_value.c_str()));
	    if (wasa->m_op != WasaQuery::OP_AND) {
		LOGERR(("wasaQueryToRcl: negative clause inside OR list!\n"));
		continue;
	    }
	    // Note: have to add dquotes which will be translated to
	    // phrase if there are several words in there. Not pretty
	    // but should work. If there is actually a single
	    // word, it will not be taken as a phrase, and
	    // stem-expansion will work normally
            // Have to do this because searchdata has nothing like and_not
	    nclause = new Rcl::SearchDataClauseSimple(Rcl::SCLT_EXCL, 
						      string("\"") + 
						      (*it)->m_value + "\"",
						      (*it)->m_fieldspec);
	    
	    if (nclause == 0) {
		reason = "Out of memory";
		LOGERR(("wasaQueryToRcl: out of memory\n"));
		return 0;
	    }
	    if ((*it)->m_modifiers & WasaQuery::WQM_NOSTEM)
		nclause->setModifiers(Rcl::SearchDataClause::SDCM_NOSTEMMING);
	    sdata->addClause(nclause);
	    break;

	case WasaQuery::OP_OR:
	    LOGDEB2(("wasaQueryToRcl: OR clause [%s]:[%s]\n", 
		     (*it)->m_fieldspec.c_str(), (*it)->m_value.c_str()));
	    // Create a subquery.
	    Rcl::SearchData *sub = 
		wasaQueryToRcl(config, *it, autosuffs, reason);
	    if (sub == 0) {
		continue;
	    }
	    nclause = 
		new Rcl::SearchDataClauseSub(Rcl::SCLT_SUB, 
					     RefCntr<Rcl::SearchData>(sub));
	    if (nclause == 0) {
		LOGERR(("wasaQueryToRcl: out of memory\n"));
		reason = "Out of memory";
		return 0;
	    }
	    if ((*it)->m_modifiers & WasaQuery::WQM_NOSTEM)
		nclause->setModifiers(Rcl::SearchDataClause::SDCM_NOSTEMMING);
	    sdata->addClause(nclause);
	}
    }

    return sdata;
}

Rcl::SearchData *wasaStringToRcl(RclConfig *config, 
				 const string &qs, string &reason, 
                                 const string& autosuffs)
{
    StringToWasaQuery parser;
    WasaQuery *wq = parser.stringToQuery(qs, reason);
    if (wq == 0) 
	return 0;
    return wasaQueryToRcl(config, wq, autosuffs, reason);
}
