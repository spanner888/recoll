/* Copyright (C) 2012 J.F.Dockes
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

#include <stdio.h>
#include <sys/types.h>
#ifdef _WIN32
#include <regex>
#else
#include <regex.h>
#endif
#include <fnmatch.h>

#include <string>

#include "cstr.h"
#include "log.h"
#include "pathut.h"
#include "strmatcher.h"

using namespace std;

bool StrWildMatcher::match(const string& val) const
{
    LOGDEB2("StrWildMatcher::match: ["  << (m_sexp) << "] against ["  << (val) << "]\n" );
    int ret = fnmatch(m_sexp.c_str(), val.c_str(), FNM_NOESCAPE);
    switch (ret) {
    case 0: return true;
    case FNM_NOMATCH: return false;
    default:
	LOGINFO("StrWildMatcher::match:err: e ["  << (m_sexp) << "] s ["  << (val) << "] ("  << (url_encode(val)) << ") ret "  << (ret) << "\n" );
	return false;
    }
}

string::size_type StrWildMatcher::baseprefixlen() const
{
    return m_sexp.find_first_of(cstr_wildSpecStChars);
}

StrRegexpMatcher::StrRegexpMatcher(const string& exp)
    : StrMatcher(exp), m_compiled(0), m_errcode(0) 
{
    setExp(exp);
}

bool StrRegexpMatcher::setExp(const string& exp)
{
    if (m_compiled) {
#ifdef _WIN32
        delete (regex*)m_compiled;
#else
	regfree((regex_t*)m_compiled);
	delete (regex_t*)m_compiled;
#endif
    }
    m_compiled = 0;
    
#ifdef _WIN32
    try {
        m_compiled = new regex(exp, std::regex_constants::nosubs | 
                               std::regex_constants::extended);
    } catch (...) {
        m_reason = string("StrRegexpMatcher:regcomp failed for ")
	    + exp + string("syntax error ?");
        return false;
    }
#else
    m_compiled = new regex_t;
    if ((m_errcode = 
	 regcomp((regex_t*)m_compiled, exp.c_str(), REG_EXTENDED|REG_NOSUB))) {
	char errbuf[200];
	regerror(m_errcode, (regex_t*)m_compiled, errbuf, 199);
	m_reason = string("StrRegexpMatcher:regcomp failed for ")
	    + exp + string(errbuf);
	return false;
    }
#endif
    m_sexp = exp;
    return true;
}

StrRegexpMatcher::~StrRegexpMatcher()
{
    if (m_compiled) {
#ifdef _WIN32
        delete (regex *)m_compiled;
#else
	regfree((regex_t*)m_compiled);
	delete (regex_t*)m_compiled;
#endif
    }
}

bool StrRegexpMatcher::match(const string& val) const
{
    if (m_errcode) 
	return false;
#ifdef _WIN32
    return regex_match(val, *((regex *)m_compiled));
#else
    return regexec((regex_t*)m_compiled, val.c_str(), 0, 0, 0) != REG_NOMATCH;
#endif
}

string::size_type StrRegexpMatcher::baseprefixlen() const
{
    return m_sexp.find_first_of(cstr_regSpecStChars);
}

bool StrRegexpMatcher::ok() const
{
    return !m_errcode;
}

