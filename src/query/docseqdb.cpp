#ifndef lint
static char rcsid[] = "@(#$Id: docseqdb.cpp,v 1.9 2008-11-13 10:57:46 dockes Exp $ (C) 2005 J.F.Dockes";
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
#include <math.h>
#include <time.h>

#include "docseqdb.h"
#include "rcldb.h"
#include "debuglog.h"
#include "internfile.h"

DocSequenceDb::DocSequenceDb(RefCntr<Rcl::Query> q, const string &t, 
			     RefCntr<Rcl::SearchData> sdata) 
    : DocSequence(t), m_q(q), m_sdata(sdata), m_fsdata(sdata),
      m_rescnt(-1), m_filt(false),
      m_queryBuildAbstract(true),
      m_queryReplaceAbstract(false)
{
}

DocSequenceDb::~DocSequenceDb() 
{
}

bool DocSequenceDb::getTerms(vector<string>& terms, 
			     vector<vector<string> >& groups, 
			     vector<int>& gslks)
{
    return m_fsdata->getTerms(terms, groups, gslks);
}

void DocSequenceDb::getUTerms(vector<string>& terms)
{
    m_sdata->getUTerms(terms);
}

string DocSequenceDb::getDescription() 
{
    return m_fsdata->getDescription();
}

bool DocSequenceDb::getDoc(int num, Rcl::Doc &doc, string *sh)
{
    if (sh) sh->erase();
    return m_q->getDoc(num, doc);
}

int DocSequenceDb::getResCnt()
{
    if (m_rescnt < 0) {
	m_rescnt= m_q->getResCnt();
    }
    return m_rescnt;
}

string DocSequenceDb::getAbstract(Rcl::Doc &doc)
{
    if (!m_q->whatDb())
	return doc.meta[Rcl::Doc::keyabs];
    string abstract;

     if (m_queryBuildAbstract && (doc.syntabs || m_queryReplaceAbstract)) {
        m_q->whatDb()->makeDocAbstract(doc, m_q.getptr(), abstract);
    } else {
        abstract = doc.meta[Rcl::Doc::keyabs];
    }

    return abstract.empty() ? doc.meta[Rcl::Doc::keyabs] : abstract;
}

bool DocSequenceDb::getEnclosing(Rcl::Doc& doc, Rcl::Doc& pdoc) 
{
    string udi;
    if (!FileInterner::getEnclosing(doc.url, doc.ipath, pdoc.url, pdoc.ipath,
                                    udi))
        return false;
    return m_q->whatDb()->getDoc(udi, pdoc);
}

list<string> DocSequenceDb::expand(Rcl::Doc &doc)
{
    return m_q->expand(doc);
}

bool DocSequenceDb::setFiltSpec(DocSeqFiltSpec &fs) 
{
    if (!fs.isNotNull()) {
	m_fsdata = m_sdata;
	m_filt = false;
	return m_q->setQuery(m_sdata);
    }

    // We build a search spec by adding a filtering layer to the base one.
    m_fsdata = RefCntr<Rcl::SearchData>(new Rcl::SearchData(Rcl::SCLT_AND));
    Rcl::SearchDataClauseSub *cl = 
	new Rcl::SearchDataClauseSub(Rcl::SCLT_SUB, m_sdata);
    m_fsdata->addClause(cl);
    
    for (unsigned int i = 0; i < fs.crits.size(); i++) {
	switch (fs.crits[i]) {
	case DocSeqFiltSpec::DSFS_MIMETYPE:
	    m_fsdata->addFiletype(fs.values[i]);
	}
    }
    m_filt = true;
    return m_q->setQuery(m_fsdata);
}

// Need a way to access the translations for filtered ...
string DocSequenceDb::title()
{
    LOGDEB(("DOcSequenceDb::title()\n"));
    return m_filt ? DocSequence::title() + " (filtered)" : DocSequence::title();
}

// TBDone
bool DocSequenceDb::setSortSpec(DocSeqSortSpec &sortspec) 
{
    return true;
}
