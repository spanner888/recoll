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
#ifndef _RCLMON_H_INCLUDED_
#define _RCLMON_H_INCLUDED_
#include "autoconfig.h"
#ifdef RCL_MONITOR

/**
 * Definitions for the real-time monitoring recoll. 
 * We're interested in file modifications, deletions and renaming.
 * We use two threads, one to receive events from the source, the
 * other to perform adequate processing.
 *
 * The two threads communicate through an event buffer which is
 * actually a hash map indexed by file path for easy coalescing of
 * multiple events to the same file.
 */

#include <string>
#include <map>

#include "rclconfig.h"

#ifndef NO_NAMESPACES
using std::string;
using std::multimap;
#endif

/**
 * Monitoring event: something changed in the filesystem
 */
class RclMonEvent {
 public: 
    enum EvType {RCLEVT_NONE, RCLEVT_MODIFY, RCLEVT_DELETE, 
		 RCLEVT_DIRCREATE};
    string m_path;
    string m_opath;
    EvType m_etyp;
    RclMonEvent() : m_etyp(RCLEVT_NONE) {}
};

enum RclMonitorOption {RCLMON_NONE=0, RCLMON_NOFORK=1, RCLMON_NOX11=2};

/**
 * Monitoring event queue. This is the shared object between the main thread 
 * (which does the actual indexing work), and the monitoring thread which 
 * receives events from FAM / inotify / etc.
 */
class RclEQData;
class RclMonEventQueue {
 public:
    RclMonEventQueue();
    ~RclMonEventQueue();
    /** Unlock queue and wait until there are new events. 
     *  Returns with the queue locked */
    bool wait(int secs = -1, bool *timedout = 0);
    /** Unlock queue */
    bool unlock();
    /** Lock queue. */
    bool lock();
    /** Lock queue and add event. */
    bool pushEvent(const RclMonEvent &ev);
    void setTerminate(); /* To all threads: end processing */
    bool ok();
    bool empty();
    RclMonEvent pop();
    void setopts(int opts);

    // Convenience function for initially communicating config to mon thr
    void setConfig(RclConfig *conf);
    RclConfig *getConfig();

 private:
    RclEQData *m_data;
};

/** Start monitoring on the topdirs described in conf */
extern bool startMonitor(RclConfig *conf, int flags);

/** Main routine for the event receiving thread */
extern void *rclMonRcvRun(void *);

// Specific debug macro for monitor synchronization events
#define MONDEB LOGDEB2


#endif // RCL_MONITOR
#endif /* _RCLMON_H_INCLUDED_ */
