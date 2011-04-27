#include "autoconfig.h"

#ifdef RCL_MONITOR
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

/**
 * Recoll real time monitor processing. This file has the code to retrieve
 * event from the event queue and do the database-side processing, and the 
 * initialization function.
 */

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "debuglog.h"
#include "rclmon.h"
#include "debuglog.h"
#include "recollindex.h"
#include "pathut.h"
#include "x11mon.h"

typedef map<string, RclMonEvent> queue_type;

/** Private part of RclEQ: things that we don't wish to exist in the interface
 *  include file.
 */
class RclEQData {
public:
    int        m_opts;
    queue_type m_queue;
    RclConfig *m_config;
    bool       m_ok;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
    RclEQData() 
	: m_config(0), m_ok(false)
    {
	if (!pthread_mutex_init(&m_mutex, 0) && !pthread_cond_init(&m_cond, 0))
	    m_ok = true;
    }
};

static RclMonEventQueue rclEQ;

RclMonEventQueue::RclMonEventQueue()
{
    m_data = new RclEQData;
}

RclMonEventQueue::~RclMonEventQueue()
{
    delete m_data;
}

bool RclMonEventQueue::empty()
{
    return m_data == 0 ? true : m_data->m_queue.empty();
}

void RclMonEventQueue::setopts(int opts)
{
    if (m_data)
	m_data->m_opts = opts;
}

// Must be called with the queue locked
RclMonEvent RclMonEventQueue::pop()
{
    RclMonEvent ev;
    if (!empty()) {
	ev = m_data->m_queue.begin()->second;
	m_data->m_queue.erase(m_data->m_queue.begin());
    }
    return ev;
}

/** Wait until there is something to process on the queue.
 *  Must be called with the queue locked 
 */
bool RclMonEventQueue::wait(int seconds, bool *top)
{
    MONDEB(("RclMonEventQueue::wait\n"));
    if (!empty()) {
	MONDEB(("RclMonEventQueue:: imm return\n"));
	return true;
    }

    int err;
    if (seconds > 0) {
	struct timespec to;
	to.tv_sec = time(0L) + seconds;
	to.tv_nsec = 0;
	if (top)
	    *top = false;
	if ((err = 
	     pthread_cond_timedwait(&m_data->m_cond, &m_data->m_mutex, &to))) {
	    if (err == ETIMEDOUT) {
		*top = true;
		MONDEB(("RclMonEventQueue:: timeout\n"));
		return true;
	    }
	    LOGERR(("RclMonEventQueue::wait:pthread_cond_timedwait failed"
		    "with err %d\n", err));
	    return false;
	}
    } else {
	if ((err = pthread_cond_wait(&m_data->m_cond, &m_data->m_mutex))) {
	    LOGERR(("RclMonEventQueue::wait: pthread_cond_wait failed"
		    "with err %d\n", err));
	    return false;
	}
    }
    MONDEB(("RclMonEventQueue:: normal return\n"));
    return true;
}

bool RclMonEventQueue::lock()
{
    MONDEB(("RclMonEventQueue:: lock\n"));
    if (pthread_mutex_lock(&m_data->m_mutex)) {
	LOGERR(("RclMonEventQueue::lock: pthread_mutex_lock failed\n"));
	return false;
    }
    MONDEB(("RclMonEventQueue:: lock return\n"));
    return true;
}
bool RclMonEventQueue::unlock()
{
    MONDEB(("RclMonEventQueue:: unlock\n"));
    if (pthread_mutex_unlock(&m_data->m_mutex)) {
	LOGERR(("RclMonEventQueue::lock: pthread_mutex_unlock failed\n"));
	return false;
    }
    return true;
}

void RclMonEventQueue::setConfig(RclConfig *cnf)
{
    m_data->m_config = cnf;
}

RclConfig *RclMonEventQueue::getConfig()
{
    return m_data->m_config;
}

bool RclMonEventQueue::ok()
{
    if (m_data == 0) {
	LOGDEB(("RclMonEventQueue: not ok: bad state\n"));
	return false;
    }
    if (stopindexing) {
	LOGDEB(("RclMonEventQueue: not ok: stop request\n"));
	return false;
    }
    if (!m_data->m_ok) {
	LOGDEB(("RclMonEventQueue: not ok: queue terminated\n"));
	return false;
    }
    return true;
}

void RclMonEventQueue::setTerminate()
{
    MONDEB(("RclMonEventQueue:: setTerminate\n"));
    lock();
    m_data->m_ok = false;
    pthread_cond_broadcast(&m_data->m_cond);
    unlock();
}

bool RclMonEventQueue::pushEvent(const RclMonEvent &ev)
{
    MONDEB(("RclMonEventQueue::pushEvent for %s\n", ev.m_path.c_str()));
    lock();
    // It seems that a newer event is always correct to override any
    // older. TBVerified ?
    m_data->m_queue[ev.m_path] = ev;
    pthread_cond_broadcast(&m_data->m_cond);
    unlock();
    return true;
}

pthread_t rcv_thrid;

bool startMonitor(RclConfig *conf, int opts)
{
    rclEQ.setConfig(conf);
    rclEQ.setopts(opts);
    if (pthread_create(&rcv_thrid, 0, &rclMonRcvRun, &rclEQ) != 0) {
	LOGERR(("startMonitor: cant create event-receiving thread\n"));
	return false;
    }

    if (!rclEQ.lock()) {
	LOGERR(("startMonitor: cant lock queue ???\n"));
	return false;
    }
    LOGDEB(("start_monitoring: entering main loop\n"));
    bool timedout;
    time_t lastauxtime = time(0);
    time_t lastixtime = lastauxtime;
    bool didsomething = false;
    const int auxinterval = 60 *60;
    const int ixinterval = 30;

    list<string> modified;
    list<string> deleted;

    // Set a relatively short timeout for better monitoring of exit requests
    while (rclEQ.wait(2, &timedout)) {
	// Queue is locked.

	// x11IsAlive() can't be called from ok() because both threads call it
	// and Xlib is not multithreaded.
        bool x11dead = !(opts & RCLMON_NOX11) && !x11IsAlive();
        if (x11dead)
            LOGDEB(("RclMonprc: x11 is dead\n"));
	if (!rclEQ.ok() || x11dead) {
	    rclEQ.unlock();
	    break;
	}
	    
	// Process event queue
	while (!rclEQ.empty()) {
	    // Retrieve event
	    RclMonEvent ev = rclEQ.pop();
	    switch (ev.m_etyp) {
	    case RclMonEvent::RCLEVT_MODIFY:
		LOGDEB(("Monitor: Modify/Check on %s\n", ev.m_path.c_str()));
		modified.push_back(ev.m_path);
		break;
	    case RclMonEvent::RCLEVT_DELETE:
		LOGDEB(("Monitor: Delete on %s\n", ev.m_path.c_str()));
		deleted.push_back(ev.m_path);
		break;
	    default:
		LOGDEB(("Monitor: got Other on %s\n", ev.m_path.c_str()));
	    }
	}
	// Unlock queue before processing lists
	rclEQ.unlock();

	// Process. We don't do this everytime but let the lists accumulate
        // a little, this saves processing.
        time_t now = time(0);
        if (now - lastixtime > ixinterval) {
            lastixtime = now;
            if (!modified.empty()) {
                modified.sort();
                modified.unique();
                if (!indexfiles(conf, modified))
                    break;
                modified.clear();
                didsomething = true;
            }
            if (!deleted.empty()) {
                deleted.sort();
                deleted.unique();
                if (!purgefiles(conf, deleted))
                    break;
                deleted.clear();
                didsomething = true;
            }
        }

	// Recreate the auxiliary dbs every hour at most.
	if (didsomething && time(0) - lastauxtime > auxinterval) {
	    lastauxtime = time(0);
	    didsomething = false;
	    if (!createAuxDbs(conf)) {
		// We used to bail out on error here. Not anymore,
		// because this is most of the time due to a failure
		// of aspell dictionary generation, which is not
		// critical.
	    }
	}

	// Lock queue before waiting again
	rclEQ.lock();
    }
    LOGDEB(("Rclmonprc: calling queue setTerminate\n"));
    rclEQ.setTerminate();
    // Wait for receiver thread before returning
    pthread_join(rcv_thrid, 0);
    LOGDEB(("Monitor: returning\n"));
    return true;
}
#endif // RCL_MONITOR
