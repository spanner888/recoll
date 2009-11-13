#ifndef lint
static char rcsid[] = "@(#$Id: recollindex.cpp,v 1.38 2008-10-14 06:07:42 dockes Exp $ (C) 2004 J.F.Dockes";
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
#ifdef HAVE_CONFIG_H
#include "autoconfig.h"
#endif

#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>

#include <iostream>
#include <list>
#include <string>
#include <cstdlib>

using namespace std;

#include "debuglog.h"
#include "rclinit.h"
#include "indexer.h"
#include "smallut.h"
#include "pathut.h"
#include "rclmon.h"
#include "x11mon.h"
#include "cancelcheck.h"
#include "rcldb.h"
#include "beaglequeue.h"
#include "recollindex.h"
#include "fsindexer.h"

// Globals for atexit cleanup
static ConfIndexer *confindexer;

// This is set as an atexit routine, 
static void cleanup()
{
    deleteZ(confindexer);
}

// Global stop request flag. This is checked in a number of place in the
// indexing routines.
int stopindexing;

// Mainly used to request indexing stop, we currently do not use the
// current file name
class MyUpdater : public DbIxStatusUpdater {
 public:
    virtual bool update() {
	if (stopindexing) {
	    return false;
	}
	return true;
    }
};
static MyUpdater updater;

static void sigcleanup(int sig)
{
    fprintf(stderr, "Got signal, registering stop request\n");
    LOGDEB(("Got signal, registering stop request\n"));
    CancelCheck::instance().setCancel();
    stopindexing = 1;
}

static bool makeIndexer(RclConfig *config)
{
    if (!confindexer)
	confindexer = new ConfIndexer(config, &updater);
    if (!confindexer) {
        cerr << "Cannot create indexer" << endl;
        exit(1);
    }
    return true;
}

// Index a list of files. We just check that they belong to one of the
// topdirs subtrees, and call the indexer method. 
//
// This is called either from the command line or from the monitor. In
// this case we're called repeatedly in the same process, and the
// confindexer is only created once by makeIndexer (but the db closed and
// flushed every time)
bool indexfiles(RclConfig *config, const list<string> &filenames)
{
    if (filenames.empty())
	return true;
    if (!makeIndexer(config))
	return false;
    return confindexer->indexFiles(filenames);
}

// Delete a list of files. Same comments about call contexts as indexfiles.
bool purgefiles(RclConfig *config, const list<string> &filenames)
{
    if (filenames.empty())
	return true;
    if (!makeIndexer(config))
	return false;
    return confindexer->purgeFiles(filenames);
}

// Create stemming and spelling databases
bool createAuxDbs(RclConfig *config)
{
    if (!makeIndexer(config))
	return false;

    if (!confindexer->createStemmingDatabases())
	return false;

    if (!confindexer->createAspellDict())
	return false;

    return true;
}

// Create additional stem database 
static bool createstemdb(RclConfig *config, const string &lang)
{
    if (!makeIndexer(config))
        return false;
    return confindexer->createStemDb(lang);
}

static const char *thisprog;
static int     op_flags;
#define OPT_MOINS 0x1
#define OPT_z     0x2 
#define OPT_h     0x4 
#define OPT_i     0x8
#define OPT_s     0x10
#define OPT_c     0x20
#define OPT_S     0x40
#define OPT_m     0x80
#define OPT_D     0x100
#define OPT_e     0x200
#define OPT_w     0x400
#define OPT_x     0x800
#define OPT_l     0x1000
#define OPT_b     0x2000

static const char usage [] =
"\n"
"recollindex [-h] \n"
"    Print help\n"
"recollindex [-z] \n"
"    Index everything according to configuration file\n"
"    -z : reset database before starting indexation\n"
#ifdef RCL_MONITOR
"recollindex -m [-w <secs>] -x [-D]\n"
"    Perform real time indexation. Don't become a daemon if -D is set.\n"
"    -w sets number of seconds to wait before starting.\n"
"    -x disables exit on end of x11 session\n"
#endif
"recollindex -e <filename [filename ...]>\n"
"    Purge data for individual files. No stem database updates\n"
"recollindex -i <filename [filename ...]>\n"
"    Index individual files. No database purge or stem database updates\n"
"recollindex -l\n"
"    List available stemming languages\n"
"recollindex -s <lang>\n"
"    Build stem database for additional language <lang>\n"
"recollindex -b\n"
"    Process the Beagle queue\n"
#ifdef RCL_USE_ASPELL
"recollindex -S\n"
"    Build aspell spelling dictionary.>\n"
#endif
"Common options:\n"
"    -c <configdir> : specify config directory, overriding $RECOLL_CONFDIR\n"
;

static void
Usage(FILE *where = stderr)
{
    FILE *fp = (op_flags & OPT_h) ? stdout : stderr;
    fprintf(fp, "%s: Usage: %s", thisprog, usage);
    fprintf(fp, "Recoll version: %s\n", Rcl::version_string().c_str());
    exit((op_flags & OPT_h)==0);
}

static RclConfig *config;
RclConfig *RclConfig::getMainConfig() 
{
    return config;
}

int main(int argc, const char **argv)
{
    string a_config;
    int sleepsecs = 60;

    thisprog = argv[0];
    argc--; argv++;

    while (argc > 0 && **argv == '-') {
	(*argv)++;
	if (!(**argv))
	    Usage();
	while (**argv)
	    switch (*(*argv)++) {
	    case 'b': op_flags |= OPT_b; break;
	    case 'c':	op_flags |= OPT_c; if (argc < 2)  Usage();
		a_config = *(++argv);
		argc--; goto b1;
#ifdef RCL_MONITOR
	    case 'D': op_flags |= OPT_D; break;
#endif
	    case 'e': op_flags |= OPT_e; break;
	    case 'h': op_flags |= OPT_h; break;
	    case 'i': op_flags |= OPT_i; break;
	    case 'l': op_flags |= OPT_l; break;
	    case 'm': op_flags |= OPT_m; break;
	    case 's': op_flags |= OPT_s; break;
#ifdef RCL_USE_ASPELL
	    case 'S': op_flags |= OPT_S; break;
#endif
	    case 'w':	op_flags |= OPT_w; if (argc < 2)  Usage();
		if ((sscanf(*(++argv), "%d", &sleepsecs)) != 1) 
		    Usage(); 
		argc--; goto b1;
	    case 'x': op_flags |= OPT_x; break;
	    case 'z': op_flags |= OPT_z; break;
	    default: Usage(); break;
	    }
    b1: argc--; argv++;
    }
    if (op_flags & OPT_h)
	Usage(stdout);
#ifndef RCL_MONITOR
    if (op_flags & (OPT_m | OPT_w|OPT_x)) {
	cerr << "Sorry, -m not available: real-time monitoring was not "
	    "configured in this build\n";
	exit(1);
    }
#endif

    if ((op_flags & OPT_z) && (op_flags & (OPT_i|OPT_e)))
	Usage();

    string reason;
    RclInitFlags flags = (op_flags & OPT_m) && !(op_flags&OPT_D) ? 
	RCLINIT_DAEMON : RCLINIT_NONE;
    config = recollinit(flags, cleanup, sigcleanup, reason, &a_config);
    if (config == 0 || !config->ok()) {
	cerr << "Configuration problem: " << reason << endl;
	exit(1);
    }
    bool rezero(op_flags & OPT_z);
    
    if (op_flags & (OPT_i|OPT_e)) {
	list<string> filenames;

	if (argc == 0) {
	    // Read from stdin
	    char line[1024];
	    while (fgets(line, 1023, stdin)) {
		string sl(line);
		trimstring(sl, "\n\r");
		filenames.push_back(sl);
	    }
	} else {
	    while (argc--) {
		filenames.push_back(*argv++);
	    }
	}
        bool status;
	if (op_flags & OPT_i)
	    status = indexfiles(config, filenames);
	else 
	    status = purgefiles(config, filenames);
        if (!confindexer->getReason().empty())
            cerr << confindexer->getReason() << endl;
        exit(status ? 0 : 1);
    } else if (op_flags & OPT_l) {
	if (argc != 0) 
	    Usage();
	list<string> stemmers = ConfIndexer::getStemmerNames();
	for (list<string>::const_iterator it = stemmers.begin(); 
	     it != stemmers.end(); it++) {
	    cout << *it << endl;
	}
	exit(0);
    } else if (op_flags & OPT_s) {
	if (argc != 1) 
	    Usage();
	string lang = *argv++; argc--;
	exit(!createstemdb(config, lang));

#ifdef RCL_MONITOR
    } else if (op_flags & OPT_m) {
	if (argc != 0) 
	    Usage();
	if (!(op_flags&OPT_D)) {
	    LOGDEB(("recollindex: daemonizing\n"));
	    daemon(0,0);
	}
	if (sleepsecs > 0) {
	    LOGDEB(("recollindex: sleeping %d\n", sleepsecs));
	    sleep(sleepsecs);
	}
	// Check that x11 did not go away while we were sleeping.
	if (!(op_flags & OPT_x) && !x11IsAlive())
	    exit(0);

	confindexer = new ConfIndexer(config, &updater);
	confindexer->index(rezero);
	deleteZ(confindexer);
	int opts = RCLMON_NONE;
	if (op_flags & OPT_D)
	    opts |= RCLMON_NOFORK;
	if (op_flags & OPT_x)
	    opts |= RCLMON_NOX11;
	bool monret = startMonitor(config, opts);
	MONDEB(("Monitor returned %d, exiting\n", monret));
	exit(monret == false);
#endif // MONITOR

#ifdef RCL_USE_ASPELL
    } else if (op_flags & OPT_S) {
	if (!makeIndexer(config))
            exit(1);
        exit(!confindexer->createAspellDict());
#endif // ASPELL
    } else if (op_flags & OPT_b) {
        cerr << "Not yet" << endl;
        return 1;
    } else {
	confindexer = new ConfIndexer(config, &updater);
	bool status = confindexer->index(rezero, ConfIndexer::IxTAll);
	if (!status) 
	    cerr << "Indexing failed" << endl;
        if (!confindexer->getReason().empty())
            cerr << confindexer->getReason() << endl;
	return !status;
    }
}
