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
#ifndef _ADVSHIST_H_INCLUDED_
#define _ADVSHIST_H_INCLUDED_
#include "autoconfig.h"

#include <vector>

#include "recoll.h"
#include <memory>
#include "searchdata.h"

/** Advanced search history. 
 *
 * We store previous searches using the "dynconf" mechanism, as string
 * entries under the "advSearchHist" key. The strings are generated by
 * translating the SearchData structure to XML, which is done by
 * calling SearchData::asXML(). 
 * When reading, we use a QXmlSimpleReader and QXmlDefaultHandler to
 * turn the XML back into a SearchData object, which is then passed to
 * the advanced search object fromSearch() method to rebuild the
 * window state.
 *
 * XML generation is performed by ../rcldb/searchdataxml.cpp. 
 * See xmltosd.h for a schema description
 */
class AdvSearchHist {
public:
    AdvSearchHist();
    ~AdvSearchHist();

    // Add entry
    bool push(std::shared_ptr<Rcl::SearchData>);

    // Get latest. does not change state
    std::shared_ptr<Rcl::SearchData> getnewest();

    // Cursor
    std::shared_ptr<Rcl::SearchData> getolder();
    std::shared_ptr<Rcl::SearchData> getnewer();

    void clear();

private:
    bool read();

    int m_current;
    std::vector<std::shared_ptr<Rcl::SearchData> > m_entries;
};

#endif // _ADVSHIST_H_INCLUDED_
