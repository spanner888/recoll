#ifndef lint
static char rcsid[] = "@(#$Id: pathut.cpp,v 1.20 2008-06-13 18:22:47 dockes Exp $ (C) 2004 J.F.Dockes";
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

#ifndef TEST_PATHUT
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <pwd.h>
#include <math.h>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <list>
#include <stack>
#ifndef NO_NAMESPACES
using std::string;
using std::list;
using std::stack;
#endif /* NO_NAMESPACES */

#include "autoconfig.h"
#include "pathut.h"

#include <sys/types.h>
#ifndef STATFS_INCLUDE
#define STATFS_INCLUDE <sys/vfs.h>
#endif

#include STATFS_INCLUDE

bool fsocc(const string &path, int *pc, long *blocks)
{
#ifdef sun
    struct statvfs buf;
    if (statvfs(path.c_str(), &buf) != 0) {
	return false;
    }
#else
    struct statfs buf;
    if (statfs(path.c_str(), &buf) != 0) {
	return false;
    }
#endif

    // used blocks
    double fpc = 0.0;
#define FSOCC_USED (double(buf.f_blocks - buf.f_bfree))
#define FSOCC_TOTAVAIL (FSOCC_USED + double(buf.f_bavail))
    if (FSOCC_TOTAVAIL > 0) {
	fpc = 100.0 * FSOCC_USED / FSOCC_TOTAVAIL;
    }
    *pc = int(fpc);
    if (blocks) {
	*blocks = 0;
#define FSOCC_MB (1024*1024)
	if (buf.f_bsize > 0) {
	    int ratio = buf.f_bsize > FSOCC_MB ? buf.f_bsize / FSOCC_MB :
		FSOCC_MB / buf.f_bsize;

	    *blocks = buf.f_bsize > FSOCC_MB ? long(buf.f_bavail) * ratio :
		long(buf.f_bavail) / ratio;
	}
    }
    return true;
}

static const char *tmplocation()
{
    const char *tmpdir = getenv("RECOLL_TMPDIR");
    if (!tmpdir)
	tmpdir = getenv("TMPDIR");
    if (!tmpdir)
	tmpdir = "/tmp";
    return tmpdir;
}

bool maketmpdir(string& tdir, string& reason)
{
    tdir = path_cat(tmplocation(), "rcltmpXXXXXX");

    char *cp = strdup(tdir.c_str());
    if (!cp) {
	reason = "maketmpdir: out of memory (for file name !)\n";
	tdir.erase();
	return false;
    }

    if (!
#ifdef HAVE_MKDTEMP
	mkdtemp(cp)
#else
	mktemp(cp)
#endif // HAVE_MKDTEMP
	) {
	free(cp);
	reason = "maketmpdir: mktemp failed\n";
	tdir.erase();
	return false;
    }	
    tdir = cp;
    free(cp);

#ifndef HAVE_MKDTEMP
    if (mkdir(tdir.c_str(), 0700) < 0) {
	reason = string("maketmpdir: mkdir ") + tdir + " failed";
	tdir.erase();
	return false;
    }
#endif

    return true;
}

TempFileInternal::TempFileInternal(const string& suffix)
{
    string filename = path_cat(tmplocation(), "rcltmpfXXXXXX");
    char *cp = strdup(filename.c_str());
    if (!cp) {
	m_reason = "Out of memory (for file name !)\n";
	return;
    }

    // Yes using mkstemp this way is awful (bot the suffix adding and
    // using mkstemp() just to avoid the warnings)
    int fd;
    if ((fd = mkstemp(cp)) < 0) {
	free(cp);
	m_reason = "maketmpdir: mkstemp failed\n";
	return;
    }
    close(fd);
    unlink(cp);
    
    filename = cp;
    free(cp);

    m_filename = filename + suffix;
    if (close(open(m_filename.c_str(), O_CREAT|O_EXCL, 0600)) != 0) {
	m_reason = string("Could not open/create") + m_filename;
	m_filename.erase();
    }
}

TempFileInternal::~TempFileInternal()
{
    if (!m_filename.empty())
	unlink(m_filename.c_str());
}


void path_catslash(std::string &s) {
    if (s.empty() || s[s.length() - 1] != '/')
	s += '/';
}

std::string path_cat(const std::string &s1, const std::string &s2) {
    std::string res = s1;
    path_catslash(res);
    res +=  s2;
    return res;
}

string path_getfather(const string &s) {
    string father = s;

    // ??
    if (father.empty())
	return "./";

    if (father[father.length() - 1] == '/') {
	// Input ends with /. Strip it, handle special case for root
	if (father.length() == 1)
	    return father;
	father.erase(father.length()-1);
    }

    string::size_type slp = father.rfind('/');
    if (slp == string::npos)
	return "./";

    father.erase(slp);
    path_catslash(father);
    return father;
}

string path_getsimple(const string &s) {
    string simple = s;

    if (simple.empty())
	return simple;

    string::size_type slp = simple.rfind('/');
    if (slp == string::npos)
	return simple;

    simple.erase(0, slp+1);
    return simple;
}

string path_basename(const string &s, const string &suff)
{
    string simple = path_getsimple(s);
    string::size_type pos = string::npos;
    if (suff.length() && simple.length() > suff.length()) {
	pos = simple.rfind(suff);
	if (pos != string::npos && pos + suff.length() == simple.length())
	    return simple.substr(0, pos);
    } 
    return simple;
}

string path_home()
{
    uid_t uid = getuid();

    struct passwd *entry = getpwuid(uid);
    if (entry == 0) {
	const char *cp = getenv("HOME");
	if (cp)
	    return cp;
	else 
	return "/";
    }

    string homedir = entry->pw_dir;
    path_catslash(homedir);
    return homedir;
}

extern string path_tildexpand(const string &s) 
{
    if (s.empty() || s[0] != '~')
	return s;
    string o = s;
    if (s.length() == 1) {
	o.replace(0, 1, path_home());
    } else if  (s[1] == '/') {
	o.replace(0, 2, path_home());
    } else {
	string::size_type pos = s.find('/');
	int l = (pos == string::npos) ? s.length() - 1 : pos - 1;
	struct passwd *entry = getpwnam(s.substr(1, l).c_str());
	if (entry)
	    o.replace(0, l+1, entry->pw_dir);
    }
    return o;
}

extern std::string path_absolute(const std::string &is)
{
    if (is.length() == 0)
	return is;
    string s = is;
    if (s[0] != '/') {
	char buf[MAXPATHLEN];
	if (!getcwd(buf, MAXPATHLEN)) {
	    return "";
	}
	s = path_cat(string(buf), s); 
    }
    return s;
}

#include <smallut.h>
extern std::string path_canon(const std::string &is)
{
    if (is.length() == 0)
	return is;
    string s = is;
    if (s[0] != '/') {
	char buf[MAXPATHLEN];
	if (!getcwd(buf, MAXPATHLEN)) {
	    return "";
	}
	s = path_cat(string(buf), s); 
    }
    list<string>elems;
    stringToTokens(s, elems, "/");
    list<string> cleaned;
    for (list<string>::const_iterator it = elems.begin(); 
	 it != elems.end(); it++){
	if (*it == "..") {
	    if (!cleaned.empty())
		cleaned.pop_back();
	} else if (it->empty() || *it == ".") {
	} else {
	    cleaned.push_back(*it);
	}
    }
    string ret;
    if (!cleaned.empty()) {
	for (list<string>::const_iterator it = cleaned.begin(); 
	     it != cleaned.end(); it++) {
	    ret += "/";
	    ret += *it;
	}
    } else {
	ret = "/";
    }
    return ret;
}

#include <glob.h>
#include <sys/stat.h>
list<std::string> path_dirglob(const std::string &dir, 
				    const std::string pattern)
{
    list<string> res;
    glob_t mglob;
    string mypat=path_cat(dir, pattern);
    if (glob(mypat.c_str(), 0, 0, &mglob)) {
	return res;
    }
    for (int i = 0; i < int(mglob.gl_pathc); i++) {
	res.push_back(mglob.gl_pathv[i]);
    }
    globfree(&mglob);
    return res;
}

bool path_isdir(const string& path)
{
    struct stat st;
    if (lstat(path.c_str(), &st) < 0) 
	return false;
    if (S_ISDIR(st.st_mode))
	return true;
    return false;
}

std::string url_encode(const std::string url, string::size_type offs)
{
    string out = url.substr(0, offs);
    const char *cp = url.c_str();
    for (string::size_type i = offs; i < url.size(); i++) {
	int c;
	const char *h = "0123456789ABCDEF";
	c = cp[i];
	if(c <= 0x1f || 
	   c >= 0x7f || 
	   c == '<' ||
	   c == '>' ||
	   c == ' ' ||
	   c == '\t'||
	   c == '"' ||
	   c == '#' ||
	   c == '%' ||
	   c == '{' ||
	   c == '}' ||
	   c == '|' ||
	   c == '\\' ||
	   c == '^' ||
	   c == '~'||
	   c == '[' ||
	   c == ']' ||
	   c == '`') {
	    out += '%';
	    out += h[(c >> 4) & 0xf];
	    out += h[c & 0xf];
	} else {
	    out += char(c);
	}
    }
    return out;
}

#else // TEST_PATHUT

#include <iostream>
using namespace std;

#include "pathut.h"

const char *tstvec[] = {"", "/", "/dir", "/dir/", "/dir1/dir2",
			 "/dir1/dir2",
			"./dir", "./dir1/", "dir", "../dir", "/dir/toto.c",
			"/dir/.c", "/dir/toto.txt", "toto.txt1"
};

const string ttvec[] = {"/dir", "", "~", "~/sub", "~root", "~root/sub",
		 "~nosuch", "~nosuch/sub"};
int nttvec = sizeof(ttvec) / sizeof(string);

const char *thisprog;

int main(int argc, const char **argv)
{
    thisprog = *argv++;argc--;

    string s;
    list<string>::const_iterator it;
#if 0
    for (unsigned int i = 0;i < sizeof(tstvec) / sizeof(char *); i++) {
	cout << tstvec[i] << " Father " << path_getfather(tstvec[i]) << endl;
    }
    for (unsigned int i = 0;i < sizeof(tstvec) / sizeof(char *); i++) {
	cout << tstvec[i] << " Simple " << path_getsimple(tstvec[i]) << endl;
    }
    for (unsigned int i = 0;i < sizeof(tstvec) / sizeof(char *); i++) {
	cout << tstvec[i] << " Basename " << 
	    path_basename(tstvec[i], ".txt") << endl;
    }
#endif

#if 0
    for (int i = 0; i < nttvec; i++) {
	cout << "tildexp: '" << ttvec[i] << "' -> '" << 
	    path_tildexpand(ttvec[i]) << "'" << endl;
    }
#endif

#if 0
    const string canontst[] = {"/dir1/../../..", "/////", "", 
			       "/dir1/../../.././/////dir2///////",
			       "../../", 
			       "../../../../../../../../../../"
    };
    unsigned int nttvec = sizeof(canontst) / sizeof(string);
    for (unsigned int i = 0; i < nttvec; i++) {
	cout << "canon: '" << canontst[i] << "' -> '" << 
	    path_canon(canontst[i]) << "'" << endl;
    }
#endif    
#if 0
    if (argc != 2) {
	fprintf(stderr, "Usage: trpathut <dir> <pattern>\n");
	exit(1);
    }
    string dir = *argv++;argc--;
    string pattern =  *argv++;argc--;
    list<string> matched = path_dirglob(dir, pattern);
    for (it = matched.begin(); it != matched.end();it++) {
	cout << *it << endl;
    }
#endif

#if 1
    if (argc != 1) {
	fprintf(stderr, "Usage: fsocc: trpathut <path>\n");
	exit(1);
    }
  string path = *argv++;argc--;

  int pc;
  long blocks;
  if (!fsocc(path, &pc, &blocks)) {
      fprintf(stderr, "fsocc failed\n");
      return 1;
  }
  printf("pc %d, megabytes %ld\n", pc, blocks);
#endif
    return 0;
}

#endif // TEST_PATHUT

