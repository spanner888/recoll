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

/**
 * Management of the auxiliary databases listing stems and their expansion 
 * terms
 */

#include "autoconfig.h"

#include <unistd.h>

#include <algorithm>
#include <map>

#include <xapian.h>

#include "stemdb.h"
#include "debuglog.h"
#include "smallut.h"
#include "synfamily.h"
#include "unacpp.h"

#include <iostream>

using namespace std;

namespace Rcl {

/**
 * Expand for one or several languages
 */
bool StemDb::stemExpand(const std::string& langs,
			const std::string& term,
			vector<string>& result)
{
    vector<string> llangs;
    stringToStrings(langs, llangs);

    for (vector<string>::const_iterator it = llangs.begin();
	 it != llangs.end(); it++) {
	SynTermTransStem stemmer(*it);
	XapComputableSynFamMember expander(getdb(), synFamStem, *it, &stemmer);
	(void)expander.synExpand(term, result);
    }

#ifndef RCL_INDEX_STRIPCHARS
    for (vector<string>::const_iterator it = llangs.begin();
	 it != llangs.end(); it++) {
	SynTermTransStem stemmer(*it);
	XapComputableSynFamMember expander(getdb(), synFamStemUnac, 
					   *it, &stemmer);
	string unac;
	unacmaybefold(term, unac, "UTF-8", UNACOP_UNAC);
	(void)expander.synExpand(unac, result);
    }
#endif 

    if (result.empty())
	result.push_back(term);

    sort(result.begin(), result.end());
    vector<string>::iterator uit = unique(result.begin(), result.end());
    result.resize(uit - result.begin());
    LOGDEB0(("stemExpand:%s: %s ->  %s\n", langs.c_str(), term.c_str(),
	     stringsToString(result).c_str()));
    return true;
}


}
