#ifndef TEST_RCLASPELL
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
#ifdef HAVE_CONFIG_H
#include "autoconfig.h"
#endif

#ifdef RCL_USE_ASPELL

#include <unistd.h>
#include <dlfcn.h>
#include <iostream>
#include <stdlib.h>
#include <vector>

#include ASPELL_INCLUDE

#include "pathut.h"
#include "execmd.h"
#include "rclaspell.h"
#include "debuglog.h"

// Just a place where we keep the Aspell library entry points together
class AspellApi {
public:
    struct AspellConfig *(*new_aspell_config)();
    int (*aspell_config_replace)(struct AspellConfig *, const char * key, 
				 const char * value);
    struct AspellCanHaveError *(*new_aspell_speller)(struct AspellConfig *);
    void (*delete_aspell_config)(struct AspellConfig *);
    void (*delete_aspell_can_have_error)(struct AspellCanHaveError *);
    struct AspellSpeller * (*to_aspell_speller)(struct AspellCanHaveError *);
    struct AspellConfig * (*aspell_speller_config)(struct AspellSpeller *);
    const struct AspellWordList * (*aspell_speller_suggest)
	(struct AspellSpeller *, const char *, int);
    int (*aspell_speller_check)(struct AspellSpeller *, const char *, int);
    struct AspellStringEnumeration * (*aspell_word_list_elements)
	(const struct AspellWordList * ths);
    const char * (*aspell_string_enumeration_next)
	(struct AspellStringEnumeration * ths);
    void (*delete_aspell_string_enumeration)(struct AspellStringEnumeration *);
    const struct AspellError *(*aspell_error)
	(const struct AspellCanHaveError *);
    const char *(*aspell_error_message)(const struct AspellCanHaveError *);
    const char *(*aspell_speller_error_message)(const struct AspellSpeller *);
    void (*delete_aspell_speller)(struct AspellSpeller *);

};
static AspellApi aapi;

#define NMTOPTR(NM, TP)							\
    if ((aapi.NM = TP dlsym(m_data->m_handle, #NM)) == 0) {		\
	badnames += #NM + string(" ");					\
    }

static const char *aspell_lib_suffixes[] = {
  ".so",
  ".so.15",
  ".so.16"
};
static const unsigned int nlibsuffs  = sizeof(aspell_lib_suffixes) / sizeof(char *);

// Stuff that we don't wish to see in the .h (possible sysdeps, etc.)
class AspellData {
public:
    AspellData() 
        : m_handle(0), m_speller(0) 
    {}
    ~AspellData() {
        LOGDEB2(("~AspellData\n"));
	if (m_handle) {
	    dlclose(m_handle);
            m_handle = 0;
        }
        if (m_speller) {
            // Dumps core if I do this?? 
            //aapi.delete_aspell_speller(m_speller);
            m_speller = 0;
            LOGDEB2(("~AspellData: speller done\n"));
        }
    }

    void  *m_handle;
    string m_exec;
    AspellSpeller *m_speller;    
};

Aspell::Aspell(RclConfig *cnf)
    : m_config(cnf), m_data(0) 
{
}

Aspell::~Aspell()
{
    deleteZ(m_data);
}

bool Aspell::init(string &reason)
{
    deleteZ(m_data);

    // Language: we get this from the configuration, else from the NLS
    // environment. The aspell language names used for selecting language 
    // definition files (used to create dictionaries) are like en, fr
    if (!m_config->getConfParam("aspellLanguage", m_lang) || m_lang.empty()) {
	string lang = "en";
	const char *cp;
	if ((cp = getenv("LC_ALL")))
	    lang = cp;
	else if ((cp = getenv("LANG")))
	    lang = cp;
	if (!lang.compare("C"))
	    lang = "en";
	m_lang = lang.substr(0, lang.find_first_of("_"));
    }

    m_data = new AspellData;

    const char *aspell_prog_from_env = getenv("ASPELL_PROG");
    if (aspell_prog_from_env && access(aspell_prog_from_env, X_OK) == 0) {
	m_data->m_exec = aspell_prog_from_env;
#ifdef ASPELL_PROG
    } else if (access(ASPELL_PROG, X_OK) == 0) {
	m_data->m_exec = ASPELL_PROG;
#endif // ASPELL_PROG
    } else {
	ExecCmd::which("aspell", m_data->m_exec);
    }

    if (m_data->m_exec.empty()) {
	reason = "aspell program not found or not executable";
        deleteZ(m_data);
	return false;
    }

    // We first look for the aspell library in libdir, and also try to
    // be clever with ASPELL_PROG.
    vector<string> libdirs;
    libdirs.push_back(LIBDIR);
    // If not in the standard place, the aspell library has to live
    // under the same prefix as the aspell program.
    {
	string aspellPrefix = path_getfather(path_getfather(m_data->m_exec));
	// This would probably require some more tweaking on solaris/irix etc.
	string dir = sizeof(long) > 4 ? "lib64" : "lib";
	string libaspell = path_cat(aspellPrefix, dir);
	if (libaspell != LIBDIR)
	    libdirs.push_back(libaspell);
    }

    reason = "Could not open shared library ";
    for (vector<string>::iterator it = libdirs.begin(); 
	 it != libdirs.end(); it++) {
	string libbase = path_cat(*it, "libaspell");
	string lib;
	for (unsigned int i = 0; i < nlibsuffs; i++) {
	    lib = libbase + aspell_lib_suffixes[i];
	    reason += string("[") + lib + "] ";
	    if ((m_data->m_handle = dlopen(lib.c_str(), RTLD_LAZY)) != 0) {
		reason.erase();
		goto found;
	    }
	}
    }
    
 found:
    if (m_data->m_handle == 0) {
        reason += string(" : ") + dlerror();
        deleteZ(m_data);
        return false;
    }

    string badnames;
    NMTOPTR(new_aspell_config, (struct AspellConfig *(*)()));
    NMTOPTR(aspell_config_replace, (int (*)(struct AspellConfig *, 
					    const char *, const char *)));
    NMTOPTR(new_aspell_speller, 
	    (struct AspellCanHaveError *(*)(struct AspellConfig *)));
    NMTOPTR(delete_aspell_config, 
	    (void (*)(struct AspellConfig *)));
    NMTOPTR(delete_aspell_can_have_error, 
	    (void (*)(struct AspellCanHaveError *)));
    NMTOPTR(to_aspell_speller, 
	    (struct AspellSpeller *(*)(struct AspellCanHaveError *)));
    NMTOPTR(aspell_speller_config, 
	    (struct AspellConfig *(*)(struct AspellSpeller *)));
    NMTOPTR(aspell_speller_suggest, 
	    (const struct AspellWordList *(*)(struct AspellSpeller *, 
					       const char *, int)));
    NMTOPTR(aspell_speller_check, 
	    (int (*)(struct AspellSpeller *, const char *, int)));
    NMTOPTR(aspell_word_list_elements, 
	    (struct AspellStringEnumeration *(*)
	     (const struct AspellWordList *)));
    NMTOPTR(aspell_string_enumeration_next, 
	    (const char * (*)(struct AspellStringEnumeration *)));
    NMTOPTR(delete_aspell_string_enumeration, 
	    (void (*)(struct AspellStringEnumeration *)));
    NMTOPTR(aspell_error, 
	    (const struct AspellError*(*)(const struct AspellCanHaveError *)));
    NMTOPTR(aspell_error_message,
	    (const char *(*)(const struct AspellCanHaveError *)));
    NMTOPTR(aspell_speller_error_message, 
	    (const char *(*)(const struct AspellSpeller *)));
    NMTOPTR(delete_aspell_speller, (void (*)(struct AspellSpeller *)));

    if (!badnames.empty()) {
	reason = string("Aspell::init: symbols not found:") + badnames;
        deleteZ(m_data);
	return false;
    }

    return true;
}

bool Aspell::ok()
{
    return m_data != 0 && m_data->m_handle != 0;
}

string Aspell::dicPath()
{
    return path_cat(m_config->getConfDir(), 
		    string("aspdict.") + m_lang + string(".rws"));
}


// The data source for the create dictionary aspell command. We walk
// the term list, filtering out things that are probably not words.
// Note that the manual for the current version (0.60) of aspell
// states that utf-8 is not well supported, so that we should maybe
// also filter all 8bit chars.
class AspExecPv : public ExecCmdProvide {
public:
    string *m_input; // pointer to string used as input buffer to command
    Rcl::TermIter *m_tit;
    Rcl::Db &m_db;
    AspExecPv(string *i, Rcl::TermIter *tit, Rcl::Db &db) 
	: m_input(i), m_tit(tit), m_db(db)
    {}
    void newData() {
	while (m_db.termWalkNext(m_tit, *m_input)) {
	    // Filter out terms beginning with upper case (special stuff) and 
	    // containing numbers, or too long. Note that the 50 limit is a
	    // byte count, so not so high if there are multibyte chars.
	    if (m_input->empty() || m_input->length() > 50)
		continue;
	    if ('A' <= m_input->at(0) && m_input->at(0) <= 'Z')
		continue;
	    if (m_input->find_first_of("0123456789.@+-,#_") != string::npos)
		continue;
	    // Got a non-empty sort-of appropriate term, let's send it to
	    // aspell
	    m_input->append("\n");
	    return;
	}
	// End of data. Tell so. Exec will close cmd.
	m_input->erase();
    }
};


bool Aspell::buildDict(Rcl::Db &db, string &reason)
{
    if (!ok())
	return false;

    // We create the dictionary by executing the aspell command:
    // aspell --lang=[lang] create master [dictApath]
    ExecCmd aspell;
    list<string> args;
    args.push_back(string("--lang=")+ m_lang);
    args.push_back("--encoding=utf-8");
    args.push_back("create");
    args.push_back("master");
    args.push_back(dicPath());
    aspell.setStderr("/dev/null");

    Rcl::TermIter *tit = db.termWalkOpen();
    if (tit == 0) {
	reason = "termWalkOpen failed\n";
	return false;
    }

    string termbuf;
    AspExecPv pv(&termbuf, tit, db);
    aspell.setProvide(&pv);

    if (aspell.doexec(m_data->m_exec, args, &termbuf)) {
	reason = string("aspell dictionary creation command failed.\n"
			"One possible reason might be missing language "
			"data files for lang = ") + m_lang;
	return false;
    }
    db.termWalkClose(tit);
    return true;
}


bool Aspell::make_speller(string& reason)
{
    if (!ok())
	return false;
    if (m_data->m_speller != 0)
        return true;

    AspellCanHaveError *ret;

    AspellConfig *config = aapi.new_aspell_config();
    aapi.aspell_config_replace(config, "lang", m_lang.c_str());
    aapi.aspell_config_replace(config, "encoding", "utf-8");
    aapi.aspell_config_replace(config, "master", dicPath().c_str());
    aapi.aspell_config_replace(config, "sug-mode", "fast");
    //    aapi.aspell_config_replace(config, "sug-edit-dist", "2");
    ret = aapi.new_aspell_speller(config);
    aapi.delete_aspell_config(config);

    if (aapi.aspell_error(ret) != 0) {
	reason = aapi.aspell_error_message(ret);
	aapi.delete_aspell_can_have_error(ret);
	return false;
    }
    m_data->m_speller = aapi.to_aspell_speller(ret);
    return true;
}

bool Aspell::check(Rcl::Db &db, const string &term, string& reason)
{
    LOGDEB2(("Aspell::check [%s]\n", term.c_str()));

    if (!ok() || !make_speller(reason))
	return false;
    if (term.empty())
        return true; //??

    int ret = aapi.aspell_speller_check(m_data->m_speller, 
                                        term.c_str(), term.length());
    reason.clear();
    switch (ret) {
    case 0: return false;
    case 1: return true;
    default:
    case -1:
        reason.append("Aspell error: ");
        reason.append(aapi.aspell_speller_error_message(m_data->m_speller));
        return false;
    }
}

bool Aspell::suggest(Rcl::Db &db, const string &term, 
                     list<string>& suggestions, string& reason)
{
    if (!ok() || !make_speller(reason))
	return false;
    if (term.empty())
        return true; //??

    AspellCanHaveError *ret;

    const AspellWordList *wl = 
	aapi.aspell_speller_suggest(m_data->m_speller, 
                                    term.c_str(), term.length());
    if (wl == 0) {
	reason = aapi.aspell_speller_error_message(m_data->m_speller);
	return false;
    }
    AspellStringEnumeration *els = aapi.aspell_word_list_elements(wl);
    const char *word;
    while ((word = aapi.aspell_string_enumeration_next(els)) != 0) {
	// stemDiffers checks that the word exists (we don't want
	// aspell computed stuff, only exact terms from the dictionary),
	// and that it stems differently to the base word (else it's not 
	// useful to expand the search). Or is it ? 
        // ******** This should depend if
	// stemming is turned on or not for querying  *******
	string sw(word);
	if (db.termExists(sw) && db.stemDiffers("english", sw, term))
	    suggestions.push_back(word);
    }
    aapi.delete_aspell_string_enumeration(els);
    return true;
}

#endif // RCL_USE_ASPELL

#else // TEST_RCLASPELL test driver ->

#ifdef HAVE_CONFIG_H
#include "autoconfig.h"
#endif

#ifdef RCL_USE_ASPELL

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <iostream>
using namespace std;

#include "rclinit.h"
#include "rclconfig.h"
#include "rcldb.h"
#include "rclaspell.h"

static char *thisprog;
RclConfig *rclconfig;
Rcl::Db rcldb;

static char usage [] =
" -b : build dictionary\n"
" -s <term>: suggestions for term\n"
"\n\n"
;
static void
Usage(void)
{
    fprintf(stderr, "%s: usage:\n%s", thisprog, usage);
    exit(1);
}

static int     op_flags;
#define OPT_MOINS 0x1
#define OPT_s	  0x2 
#define OPT_b	  0x4 

int main(int argc, char **argv)
{
    string word;
    
    thisprog = argv[0];
    argc--; argv++;

    while (argc > 0 && **argv == '-') {
	(*argv)++;
	if (!(**argv))
	    /* Cas du "adb - core" */
	    Usage();
	while (**argv)
	    switch (*(*argv)++) {
	    case 'b':	op_flags |= OPT_b; break;
	    case 's':	op_flags |= OPT_s; if (argc < 2)  Usage();
		word = *(++argv);
		argc--; 
		goto b1;
	    default: Usage();	break;
	    }
    b1: argc--; argv++;
    }

    if (argc != 0 || op_flags == 0)
	Usage();

    string reason;
    rclconfig = recollinit(0, 0, reason);
    if (!rclconfig || !rclconfig->ok()) {
	fprintf(stderr, "Configuration problem: %s\n", reason.c_str());
	exit(1);
    }

    string dbdir = rclconfig->getDbDir();
    if (dbdir.empty()) {
	fprintf(stderr, "No db directory in configuration");
	exit(1);
    }

    if (!rcldb.open(dbdir, Rcl::Db::DbRO, 0)) {
	fprintf(stderr, "Could not open database in %s\n", dbdir.c_str());
	exit(1);
    }

    Aspell aspell(rclconfig);

    if (!aspell.init(reason)) {
	cerr << "Init failed: " << reason << endl;
	exit(1);
    }
    if (op_flags & OPT_b) {
	if (!aspell.buildDict(rcldb, reason)) {
	    cerr << "buildDict failed: " << reason << endl;
	    exit(1);
	}
    } else {
	list<string> suggs;
	if (!aspell.suggest(rcldb, word, suggs, reason)) {
	    cerr << "suggest failed: " << reason << endl;
	    exit(1);
	}
	cout << "Suggestions for " << word << ":" << endl;
	for (list<string>::iterator it = suggs.begin(); 
	     it != suggs.end(); it++) {
	    cout << *it << endl;
	}
    }
    exit(0);
}
#else
int main(int argc, char **argv)
{return 1;}
#endif // RCL_USE_ASPELL

#endif // TEST_RCLASPELL test driver
