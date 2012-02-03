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

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <qthread.h>
#include <qmutex.h>
#include <qwaitcondition.h>

#include "indexer.h"
#include "debuglog.h"
#include "idxthread.h"
#include "smallut.h"
#include "rclinit.h"
#include "pathut.h"
#include "recoll.h"

static int stopindexing;
static int startindexing;
static bool rezero;
static vector<Rcl::Doc> idxdocs; // Docs to update. Update all if empty

static IdxThreadStatus indexingstatus = IDXTS_OK;
static string indexingReason;
static int stopidxthread;

static QMutex curfile_mutex;
static QMutex         action_mutex;
static QWaitCondition action_wait;

class IdxThread : public QThread , public DbIxStatusUpdater {
    virtual void run();
 public:
    // This gets called at intervals by the file system walker to check for 
    // a requested interrupt and update the current status.
    virtual bool update() {
	QMutexLocker locker(&curfile_mutex);
	m_statusSnap = status;
	LOGDEB1(("IdxThread::update: indexing %s\n", m_statusSnap.fn.c_str()));
	if (stopindexing) {
	    stopindexing = 0;
	    m_interrupted = true;
	    return false;
	}
	return true;
    }
    // Maintain a copy/snapshot of idx status
    DbIxStatus m_statusSnap;
    bool m_interrupted;
};

void IdxThread::run()
{
    recoll_threadinit();
    action_mutex.lock();
    for (;;) {
	if (startindexing) {
	    startindexing = 0;
            action_mutex.unlock();

	    m_interrupted = false;
	    indexingstatus = IDXTS_NULL;
	    // We make a private snapshot of the config: setKeydir changes
	    // it during indexing and it may be updated by the main thread.
	    RclConfig *myconf;
	    {
		PTMutexLocker locker(thestableconfiglock);
		myconf = new RclConfig(*thestableconfig);
	    }
	    int loglevel;
	    myconf->setKeyDir("");
	    myconf->getConfParam("loglevel", &loglevel);
	    DebugLog::getdbl()->setloglevel(loglevel);

	    Pidfile pidfile(myconf->getPidfile());
	    if (pidfile.open() != 0) {
                // Have to sleep a little here else the main thread
                // won't see the status change because it only tests
                // after seeing IDXTS_NULL at least once. Better
                // interlock needed...
                sleep(2);
		indexingstatus = IDXTS_ERROR;
		indexingReason = "Indexing failed: other process active? " + 
		    pidfile.getreason();
	    } else {
		pidfile.write_pid();
		ConfIndexer *indexer = new ConfIndexer(myconf, this);
		bool status = idxdocs.empty() ? 
		    indexer->index(rezero, ConfIndexer::IxTAll) :
		    indexer->updateDocs(idxdocs);
		    
		if (status) {
		    indexingstatus = IDXTS_OK;
		    indexingReason = "";
		} else {
		    indexingstatus = IDXTS_ERROR;
		    indexingReason = "Indexing failed: " + indexer->getReason();
		}
		pidfile.close();
		delete indexer;
	    }
	    delete myconf;
	    myconf = 0;
	    rezero = false;
            action_mutex.lock();
	}

	if (stopidxthread) {
            action_mutex.unlock();
	    return;
	}
        action_wait.wait(&action_mutex);
    }
}

static IdxThread idxthread;

// Functions called by the main thread

void start_idxthread()
{
    idxthread.start();
}

void stop_idxthread()
{
    action_mutex.lock();
    startindexing = 0;
    stopindexing = 1;
    stopidxthread = 1;
    action_mutex.unlock();
    action_wait.wakeAll();
    idxthread.wait();
}

void stop_indexing()
{
    action_mutex.lock();
    startindexing = 0;
    stopindexing = 1;
    action_mutex.unlock();
    action_wait.wakeAll();
}

void start_indexing(bool raz, vector<Rcl::Doc> docs)
{
    action_mutex.lock();
    startindexing = 1;
    rezero = raz;
    idxdocs = docs;
    action_mutex.unlock();
    action_wait.wakeAll();
}

DbIxStatus idxthread_idxStatus()
{
    QMutexLocker locker(&curfile_mutex);
    return idxthread.m_statusSnap;
}

bool idxthread_idxInterrupted()
{
    return idxthread.m_interrupted;
}

string idxthread_getReason()
{
    return indexingReason;
}

IdxThreadStatus idxthread_getStatus()
{
    return indexingstatus;
}
