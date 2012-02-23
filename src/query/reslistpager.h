/* Copyright (C) 2007 J.F.Dockes
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

#ifndef _reslistpager_h_included_
#define _reslistpager_h_included_

#include <vector>
using std::vector;

#include "refcntr.h"
#include "docseq.h"

class RclConfig;
class PlainToRich;
class HiliteData;

/**
 * Manage a paged HTML result list. 
 */
class ResListPager {
public:
    ResListPager(int pagesize=10);
    virtual ~ResListPager() {}

    void setHighLighter(PlainToRich *ptr) 
    {
        m_hiliter = ptr;
    }
    void setDocSource(RefCntr<DocSequence> src, int winfirst = -1)
    {
        m_pagesize = m_newpagesize;
	m_winfirst = winfirst;
	m_hasNext = false;
	m_docSource = src;
	m_respage.clear();
    }
    void setPageSize(int ps) 
    {
        m_newpagesize = ps;
    }
    int pageNumber() 
    {
	if (m_winfirst < 0 || m_pagesize <= 0)
	    return -1;
	return m_winfirst / m_pagesize;
    }
    int pageFirstDocNum() {
	return m_winfirst;
    }
    int pageLastDocNum() {
	if (m_winfirst < 0 || m_respage.size() == 0)
	    return -1;
	return m_winfirst + m_respage.size() - 1;
    }
    virtual int pageSize() const {return m_pagesize;}
    void pageNext();
    bool hasNext() {return m_hasNext;}
    bool hasPrev() {return m_winfirst > 0;}
    bool atBot() {return m_winfirst <= 0;}
    void resultPageFirst() {
	m_winfirst = -1;
        m_pagesize = m_newpagesize;
	resultPageNext();
    }
    void resultPageBack() {
	if (m_winfirst <= 0) return;
	m_winfirst -= 2 * m_pagesize;
	resultPageNext();
    }
    void resultPageNext();
    void resultPageFor(int docnum);
    void displayPage(RclConfig *);
    void displayDoc(RclConfig *, int idx, Rcl::Doc& doc, 
		    const HiliteData& hdata, const string& sh = "");
    bool pageEmpty() {return m_respage.size() == 0;}

    string queryDescription() {return m_docSource.isNull() ? "" :
	    m_docSource->getDescription();}

    // Things that need to be reimplemented in the subclass:
    virtual bool append(const string& data);
    virtual bool append(const string& data, int, const Rcl::Doc&)
    {
	return append(data);
    }
    // Translation function. This is reimplemented in the qt reslist
    // object For this to work, the strings must be duplicated inside
    // reslist.cpp (see the QT_TR_NOOP in there). Very very unwieldy.
    // To repeat: any change to a string used with trans() inside
    // reslistpager.cpp must be reflected in the string table inside
    // reslist.cpp for translation to work.
    virtual string trans(const string& in);
    virtual string detailsLink();
    virtual const string &parFormat();
    virtual const string &dateFormat();
    virtual string nextUrl();
    virtual string prevUrl();
    virtual string pageTop() {return string();}
    virtual string headerContent() {return string();}
    virtual string iconUrl(RclConfig *, Rcl::Doc& doc);
    virtual void suggest(const vector<string>, 
			 map<string, vector<string> >& sugg) {
        sugg.clear();
    }
    virtual string absSep() {return "&hellip;";}
private:
    int                  m_pagesize;
    int                  m_newpagesize;
    // First docnum (from docseq) in current page
    int                  m_winfirst;
    bool                 m_hasNext;
    PlainToRich         *m_hiliter;
    RefCntr<DocSequence> m_docSource;
    vector<ResListEntry> m_respage;
};

#endif /* _reslistpager_h_included_ */
