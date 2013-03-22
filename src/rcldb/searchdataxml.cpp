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

// Handle translation from rcl's SearchData structures to XML. Used for
// complex search history storage in the GUI

#include "autoconfig.h"

#include <stdio.h>

#include <string>
#include <vector>
#include <sstream>
using namespace std;

#include "searchdata.h"
#include "debuglog.h"
#include "base64.h"

namespace Rcl {

static string tpToString(SClType tp)
{
    switch (tp) {
    case SCLT_AND: return "AND";
    case SCLT_OR: return "OR";
    case SCLT_FILENAME: return "FN";
    case SCLT_PHRASE: return "PH";
    case SCLT_NEAR: return "NE";
    case SCLT_SUB: return "SU"; // Unsupported actually
    default: return "UN";
    }
}

string SearchData::asXML()
{
    LOGDEB(("SearchData::asXML\n"));
    ostringstream os;

    // Searchdata
    os << "<SD>" << endl;

    // Clause list
    os << "<CL>" << endl;
    if (m_tp != SCLT_AND)
	os << "<CLT>" << tpToString(m_tp) << "</CLT>" << endl;
    for (unsigned int i = 0; i <  m_query.size(); i++) {
	SearchDataClause *c = m_query[i];
	if (c->getTp() == SCLT_SUB) {
	    LOGERR(("SearchData::asXML: can't do subclauses !\n"));
	    continue;
	}
	if (c->getexclude())
	    os << "<NEG/>" << endl;
	if (c->getTp() == SCLT_PATH) {
	    // Keep these apart, for compat with the older history format
	    SearchDataClausePath *cl = 
		dynamic_cast<SearchDataClausePath*>(c);
	    if (cl->getexclude()) {
		os << "<ND>" << base64_encode(cl->gettext()) << "</ND>" << endl;
	    } else {
		os << "<YD>" << base64_encode(cl->gettext()) << "</YD>" << endl;
	    }
	    continue;
	}

	SearchDataClauseSimple *cl = 
	    dynamic_cast<SearchDataClauseSimple*>(c);
	os << "<C>" << endl;
	if (cl->getTp() != SCLT_AND) {
	    os << "<CT>" << tpToString(cl->getTp()) << "</CT>" << endl;
	}
	if (cl->getTp() != SCLT_FILENAME && !cl->getfield().empty()) {
	    os << "<F>" << base64_encode(cl->getfield()) << "</F>" << endl;
	}
	os << "<T>" << base64_encode(cl->gettext()) << "</T>" << endl;
	if (cl->getTp() == SCLT_NEAR || cl->getTp() == SCLT_PHRASE) {
	    SearchDataClauseDist *cld = 
	    dynamic_cast<SearchDataClauseDist*>(cl);
	    os << "<S>" << cld->getslack() << "</S>" << endl;
	}
	os << "</C>" << endl;
    }
    os << "</CL>" << endl;

    if (m_haveDates) {
	if (m_dates.y1 > 0) {
	    os << "<DMI>" << 
		"<D>" << m_dates.d1 << "</D>" <<
		"<M>" << m_dates.m1 << "</M>" << 
		"<Y>" << m_dates.y1 << "</Y>" 
	       << "</DMI>" << endl;
	}
	if (m_dates.y2 > 0) {
	    os << "<DMA>" << 
		"<D>" << m_dates.d2 << "</D>" <<
		"<M>" << m_dates.m2 << "</M>" << 
		"<Y>" << m_dates.y2 << "</Y>" 
	       << "</DMA>" << endl;
	}
    }

    if (m_minSize != size_t(-1)) {
	os << "<MIS>" << m_minSize << "</MIS>" << endl;
    }
    if (m_maxSize != size_t(-1)) {
	os << "<MAS>" << m_maxSize << "</MAS>" << endl;
    }

    if (!m_filetypes.empty()) {
	os << "<ST>";
	for (vector<string>::iterator it = m_filetypes.begin(); 
	     it != m_filetypes.end(); it++) {
	    os << *it << " ";
	}
	os << "</ST>" << endl;
    }

    if (!m_nfiletypes.empty()) {
	os << "<IT>";
	for (vector<string>::iterator it = m_nfiletypes.begin(); 
	     it != m_nfiletypes.end(); it++) {
	    os << *it << " ";
	}
	os << "</IT>" << endl;
    }

    os << "</SD>";
    return os.str();
}


}