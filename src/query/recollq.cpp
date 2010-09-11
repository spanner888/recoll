#ifndef lint
static char rcsid[] = "@(#$Id: recollq.cpp,v 1.22 2008-12-17 08:01:40 dockes Exp $ (C) 2006 J.F.Dockes";
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
// Takes a query and run it, no gui, results to stdout

#ifndef TEST_RECOLLQ
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#include <iostream>
#include <list>
#include <string>
using namespace std;

#include "rcldb.h"
#include "rclquery.h"
#include "rclconfig.h"
#include "pathut.h"
#include "rclinit.h"
#include "debuglog.h"
#include "wasastringtoquery.h"
#include "wasatorcl.h"
#include "internfile.h"
#include "wipedir.h"
#include "transcode.h"
#include "textsplit.h"

bool dump_contents(RclConfig *rclconfig, TempDir& tmpdir, Rcl::Doc& idoc)
{
    FileInterner interner(idoc, rclconfig, tmpdir,
                          FileInterner::FIF_forPreview);
    Rcl::Doc fdoc;
    string ipath = idoc.ipath;
    if (interner.internfile(fdoc, ipath)) {
	cout << fdoc.text << endl;
    } else {
	cout << "Cant turn to text:" << idoc.url << " | " << idoc.ipath << endl;
    }
    return true;
}


static char *thisprog;
static char usage [] =
" -P: Show date span for documents in index\n"
" [-o|-a|-f] <query string>\n"
" Runs a recoll query and displays result lines. \n"
"  Default: will interpret the argument(s) as a xesam query string\n"
"    query may be like: \n"
"    implicit AND, Exclusion, field spec:    t1 -t2 title:t3\n"
"    OR has priority: t1 OR t2 t3 OR t4 means (t1 OR t2) AND (t3 OR t4)\n"
"    Phrase: \"t1 t2\" (needs additional quoting on cmd line)\n"
"  -o Emulate the gui simple search in ANY TERM mode\n"
"  -a Emulate the gui simple search in ALL TERMS mode\n"
"  -f Emulate the gui simple search in filename mode\n"
"Common options:\n"
"    -c <configdir> : specify config directory, overriding $RECOLL_CONFDIR\n"
"    -d also dump file contents\n"
"    -n <cnt> limit the maximum number of results (0->no limit, default 2000)\n"
"    -b : basic. Just output urls, no mime types or titles\n"
"    -m : dump the whole document meta[] array\n"
"    -A : output the document abstracts\n"
"    -S fld : sort by field name\n"
"    -D : sort descending\n"
"    -i <dbdir> : additional index, several can be given\n"
;
static void
Usage(void)
{
    cerr << thisprog <<  ": usage:" << endl << usage;
    exit(1);
}

// ATTENTION A LA COMPATIBILITE AVEC LES OPTIONS DE recoll
// OPT_q and OPT_t are ignored
static int     op_flags;
#define OPT_o     0x2 
#define OPT_a     0x4 
#define OPT_c     0x8
#define OPT_d     0x10
#define OPT_n     0x20
#define OPT_b     0x40
#define OPT_f     0x80
#define OPT_l     0x100
#define OPT_q     0x200
#define OPT_t     0x400
#define OPT_m     0x800
#define OPT_D     0x1000
#define OPT_S     0x2000
#define OPT_s     0x4000
#define OPT_A     0x8000
#define OPT_i     0x10000
#define OPT_P     0x20000

int recollq(RclConfig **cfp, int argc, char **argv)
{
    string a_config;
    string sortfield;
    string stemlang("english");
    list<string> extra_dbs;

    int limit = 2000;
    thisprog = argv[0];
    argc--; argv++;

    while (argc > 0 && **argv == '-') {
        (*argv)++;
        if (!(**argv))
            /* Cas du "adb - core" */
            Usage();
        while (**argv)
            switch (*(*argv)++) {
            case 'A':   op_flags |= OPT_A; break;
            case 'a':   op_flags |= OPT_a; break;
            case 'b':   op_flags |= OPT_b; break;
	    case 'c':	op_flags |= OPT_c; if (argc < 2)  Usage();
		a_config = *(++argv);
		argc--; goto b1;
            case 'd':   op_flags |= OPT_d; break;
            case 'D':   op_flags |= OPT_D; break;
            case 'f':   op_flags |= OPT_f; break;
	    case 'i':	op_flags |= OPT_i; if (argc < 2)  Usage();
		extra_dbs.push_back(*(++argv));
		argc--; goto b1;
            case 'l':   op_flags |= OPT_l; break;
            case 'm':   op_flags |= OPT_m; break;
	    case 'n':	op_flags |= OPT_n; if (argc < 2)  Usage();
		limit = atoi(*(++argv));
		if (limit <= 0) limit = INT_MAX;
		argc--; goto b1;
            case 'o':   op_flags |= OPT_o; break;
            case 'P':   op_flags |= OPT_P; break;
            case 'q':   op_flags |= OPT_q; break;
	    case 'S':	op_flags |= OPT_S; if (argc < 2)  Usage();
		sortfield = *(++argv);
		argc--; goto b1;
	    case 's':	op_flags |= OPT_s; if (argc < 2)  Usage();
		stemlang = *(++argv);
		argc--; goto b1;
            case 't':   op_flags |= OPT_t; break;
            default: Usage();   break;
            }
    b1: argc--; argv++;
    }

    string reason;
    *cfp = recollinit(0, 0, reason, &a_config);
    RclConfig *rclconfig = *cfp;
    if (!rclconfig || !rclconfig->ok()) {
	fprintf(stderr, "Recoll init failed: %s\n", reason.c_str());
	exit(1);
    }

    if (argc < 1 && !(op_flags & OPT_P)) {
	Usage();
    }

    Rcl::Db rcldb(rclconfig);
    if (!extra_dbs.empty()) {
        for (list<string>::iterator it = extra_dbs.begin();
             it != extra_dbs.end(); it++) {
            if (!rcldb.addQueryDb(*it)) {
                cerr << "Can't add index: " << *it << endl;
                exit(1);
            }
        }
    }

    if (!rcldb.open(Rcl::Db::DbRO)) {
	cerr << "Cant open database in " << rclconfig->getDbDir() << 
	    " reason: " << rcldb.getReason() << endl;
	exit(1);
    }

    if (op_flags & OPT_P) {
        int minyear, maxyear;
        if (!rcldb.maxYearSpan(&minyear, &maxyear)) {
            cerr << "maxYearSpan failed: " << rcldb.getReason() << endl;
            exit(1);
        } else {
            cout << "Min year " << minyear << " Max year " << maxyear << endl;
            exit(0);
        }
    }

    if (argc < 1) {
	Usage();
    }
    string qs = *argv++;argc--;
    while (argc > 0) {
	qs += string(" ") + *argv++;argc--;
    }

    {
	string uq;
	string charset = rclconfig->getDefCharset(true);
	int ercnt;
	if (!transcode(qs, uq, charset, "UTF-8", &ercnt)) {
	    fprintf(stderr, "Can't convert command line args to utf-8\n");
	    exit(1);
	} else if (ercnt) {
	    fprintf(stderr, "%d errors while converting arguments from %s "
		    "to utf-8\n", ercnt, charset.c_str());
	}
	qs = uq;
    }

    Rcl::SearchData *sd = 0;

    if (op_flags & (OPT_a|OPT_o|OPT_f)) {
	sd = new Rcl::SearchData(Rcl::SCLT_OR);
	Rcl::SearchDataClause *clp = 0;
	if (op_flags & OPT_f) {
	    clp = new Rcl::SearchDataClauseFilename(qs);
	} else {
	    // If there is no white space inside the query, then the user
	    // certainly means it as a phrase.
	    bool isreallyaphrase = false;
	    if (!TextSplit::hasVisibleWhite(qs))
		isreallyaphrase = true;
	    clp = isreallyaphrase ? 
		new Rcl::SearchDataClauseDist(Rcl::SCLT_PHRASE, qs, 0) :
		new Rcl::SearchDataClauseSimple((op_flags & OPT_o)?
						Rcl::SCLT_OR : Rcl::SCLT_AND, 
						qs);
	}
	if (sd)
	    sd->addClause(clp);
    } else {
	sd = wasaStringToRcl(qs, reason);
    }

    if (!sd) {
	cerr << "Query string interpretation failed: " << reason << endl;
	return 1;
    }
    sd->setStemlang(stemlang);

    RefCntr<Rcl::SearchData> rq(sd);
    Rcl::Query query(&rcldb);
    if (op_flags & OPT_S) {
	query.setSortBy(sortfield, (op_flags & OPT_D) ? false : true);
    }
    query.setQuery(rq);
    int cnt = query.getResCnt();
    if (!(op_flags & OPT_b)) {
	cout << "Recoll query: " << rq->getDescription() << endl;
	if (cnt <= limit)
	    cout << cnt << " results" << endl;
	else
	    cout << cnt << " results (printing  " << limit << " max):" << endl;
    }

    TempDir tmpdir;
    if (!tmpdir.ok()) {
	cerr << "Can't create temporary directory: " << 
	    tmpdir.getreason() << endl;
	exit(1);
    }
    for (int i = 0; i < limit; i++) {
	Rcl::Doc doc;
	if (!query.getDoc(i, doc))
	    break;

	if (op_flags & OPT_b) {
	    cout << doc.url.c_str() << endl;
	} else {
	    char cpc[20];
	    sprintf(cpc, "%d", doc.pc);
	    cout 
		<< doc.mimetype.c_str() << "\t"
		<< "[" << doc.url.c_str() << "]" << "\t" 
		<< "[" << doc.meta[Rcl::Doc::keytt].c_str() << "]" << "\t"
		<< doc.fbytes.c_str()   << "\tbytes" << "\t"
		<<  endl;
	    if (op_flags & OPT_m) {
		for (map<string,string>::const_iterator it = doc.meta.begin();
		     it != doc.meta.end(); it++) {
		    cout << it->first << " = " << it->second << endl;
		}
	    }
            if (op_flags & OPT_A) {
                string abstract;
                if (rcldb.makeDocAbstract(doc, &query, abstract)) {
                    cout << "ABSTRACT" << endl;
                    cout << abstract << endl;
                    cout << "/ABSTRACT" << endl;
                }
            }
        }
        if (op_flags & OPT_d) {
            dump_contents(rclconfig, tmpdir, doc);
        }	
    }

    return 0;
}

#else // TEST_RECOLLQ The test driver is actually the useful program...
#include <stdlib.h>

#include "rclconfig.h"
#include "recollq.h"

static RclConfig *rclconfig;

RclConfig *RclConfig::getMainConfig() 
{
    return rclconfig;
}

int main(int argc, char **argv)
{
    exit(recollq(&rclconfig, argc, argv));
}
#endif // TEST_RECOLLQ
