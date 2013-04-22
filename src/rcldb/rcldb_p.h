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

#ifndef _rcldb_p_h_included_
#define _rcldb_p_h_included_

#include "autoconfig.h"

#include <map>

#include <xapian.h>

#ifdef IDX_THREADS
#include "workqueue.h"
#endif // IDX_THREADS
#include "debuglog.h"
#include "xmacros.h"
#include "ptmutex.h"

namespace Rcl {

class Query;

#ifdef IDX_THREADS
// Task for the index update thread. This can be 
//  - add/update for a new / update documment
//  - delete for a deleted document
//  - purgeOrphans when a multidoc file is updated during a partial pass (no 
//    general purge). We want to remove subDocs that possibly don't
//    exist anymore. We find them by their different sig
// txtlen and doc are only valid for add/update else, len is (size_t)-1 and doc
// is empty
class DbUpdTask {
public:
    enum Op {AddOrUpdate, Delete, PurgeOrphans};
    // Note that udi and uniterm are strictly equivalent and are
    // passed both just to avoid recomputing uniterm which is
    // available on the caller site.
    DbUpdTask(Op _op, const string& ud, const string& un, 
	      const Xapian::Document &d, size_t tl)
	: op(_op), udi(ud), uniterm(un), doc(d), txtlen(tl)
    {}
    // Udi and uniterm equivalently designate the doc
    Op op;
    string udi;
    string uniterm;
    Xapian::Document doc;
    // txtlen is used to update the flush interval. It's -1 for a
    // purge because we actually don't know it, and the code fakes a
    // text length based on the term count.
    size_t txtlen;
};
#endif // IDX_THREADS

// A class for data and methods that would have to expose
// Xapian-specific stuff if they were in Rcl::Db. There could actually be
// 2 different ones for indexing or query as there is not much in
// common.
class Db::Native {
 public:
    Db  *m_rcldb; // Parent
    bool m_isopen;
    bool m_iswritable;
    bool m_noversionwrite; //Set if open failed because of version mismatch!
#ifdef IDX_THREADS
    WorkQueue<DbUpdTask*> m_wqueue;
    int  m_loglevel;
    PTMutexInit m_mutex;
    long long  m_totalworkns;
    bool m_havewriteq;
    void maybeStartThreads();
#endif // IDX_THREADS

    // Indexing 
    Xapian::WritableDatabase xwdb;
    // Querying (active even if the wdb is too)
    Xapian::Database xrdb;

    Native(Db *db);
    ~Native();

#ifdef IDX_THREADS
    friend void *DbUpdWorker(void*);
#endif // IDX_THREADS

    // Final steps of doc update, part which need to be single-threaded
    bool addOrUpdateWrite(const string& udi, const string& uniterm, 
			  Xapian::Document& doc, size_t txtlen);

    bool purgeFileWrite(bool onlyOrphans, const string& udi, 
			const string& uniterm);
    bool getPagePositions(Xapian::docid docid, vector<int>& vpos);
    int getPageNumberForPosition(const vector<int>& pbreaks, unsigned int pos);

    bool dbDataToRclDoc(Xapian::docid docid, std::string &data, Doc &doc);

    /** Compute list of subdocuments for a given udi. We look for documents 
     * indexed by a parent term matching the udi, the posting list for the 
     * parentterm(udi)  (As suggested by James Aylett)
     *
     * Note that this is not currently recursive: all subdocs are supposed 
     * to be children of the file doc.
     * Ie: in a mail folder, all messages, attachments, attachments of
     * attached messages etc. must have the folder file document as
     * parent. 
     * Parent-child relationships are defined by the indexer (rcldb user)
     * 
     * The file-system indexer currently works this way (flatly), 
     * subDocs() could be relatively easily changed to support full recursivity
     * if needed.
     */
    bool subDocs(const string &udi, vector<Xapian::docid>& docids);

};

// This is the word position offset at which we index the body text
// (abstract, keywords, etc.. are stored before this)
static const unsigned int baseTextPosition = 100000;

}
#endif /* _rcldb_p_h_included_ */
