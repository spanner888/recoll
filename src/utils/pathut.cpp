/* Copyright (C) 2004 J.F.Dockes
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
#include "autoconfig.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <pwd.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

// Let's include all files where statfs can be defined and hope for no
// conflict...
#ifdef HAVE_SYS_MOUNT_H 
#include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATFS_H 
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_STATVFS_H 
#include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H 
#include <sys/vfs.h>
#endif

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stack>
#ifndef NO_NAMESPACES
using std::string;
using std::stack;
#endif /* NO_NAMESPACES */

#include "pathut.h"
#include "transcode.h"
#include "wipedir.h"
#include "md5.h"

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
	reason = "maketmpdir: mktemp failed for [" + tdir + "] : " +
	    strerror(errno);
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
	m_reason = "TempFileInternal: mkstemp failed\n";
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

TempDir::TempDir()
{
    if (!maketmpdir(m_dirname, m_reason)) {
	m_dirname.erase();
	return;
    }
}

TempDir::~TempDir()
{
    if (!m_dirname.empty()) {
	(void)wipedir(m_dirname, true, true);
	m_dirname.erase();
    }
}

bool TempDir::wipe()
{
    if (m_dirname.empty()) {
	m_reason = "TempDir::wipe: no directory !\n";
	return false;
    }
    if (wipedir(m_dirname, false, true)) {
	m_reason = "TempDir::wipe: wipedir failed\n";
	return false;
    }
    return true;
}

void path_catslash(string &s) {
    if (s.empty() || s[s.length() - 1] != '/')
	s += '/';
}

string path_cat(const string &s1, const string &s2) {
    string res = s1;
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

extern string path_absolute(const string &is)
{
    if (is.length() == 0)
	return is;
    string s = is;
    if (s[0] != '/') {
	char buf[MAXPATHLEN];
	if (!getcwd(buf, MAXPATHLEN)) {
	    return string();
	}
	s = path_cat(string(buf), s); 
    }
    return s;
}

#include <smallut.h>
extern string path_canon(const string &is)
{
    if (is.length() == 0)
	return is;
    string s = is;
    if (s[0] != '/') {
	char buf[MAXPATHLEN];
	if (!getcwd(buf, MAXPATHLEN)) {
	    return string();
	}
	s = path_cat(string(buf), s); 
    }
    vector<string> elems;
    stringToTokens(s, elems, "/");
    vector<string> cleaned;
    for (vector<string>::const_iterator it = elems.begin(); 
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
	for (vector<string>::const_iterator it = cleaned.begin(); 
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
vector<string> path_dirglob(const string &dir, 
			    const string pattern)
{
    vector<string> res;
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

// Allowed punctuation in the path part of an URI according to RFC2396
// -_.!~*'():@&=+$,
/*
21 !
    22 "
    23 #
24 $
    25 %
26 &
27 '
28 (
29 )
2A *
2B +
2C , 
2D -
2E .
2F /
30 0
...
39 9
3A :
    3B ;
    3C <
3D =
    3E >
    3F ?
40 @
41 A
...
5A Z
    5B [
    5C \
    5D ]
    5E ^
5F _
    60 `
61 a
...
7A z
    7B {
    7C |
    7D }
7E ~
    7F DEL
*/
string url_encode(const string& url, string::size_type offs)
{
    string out = url.substr(0, offs);
    const char *cp = url.c_str();
    for (string::size_type i = offs; i < url.size(); i++) {
	int c;
	const char *h = "0123456789ABCDEF";
	c = cp[i];
	if (c <= 0x20 || 
	   c >= 0x7f || 
	   c == '"' ||
	   c == '#' ||
	   c == '%' ||
	   c == ';' ||
	   c == '<' ||
	   c == '>' ||
	   c == '?' ||
	   c == '[' ||
	   c == '\\' ||
	   c == ']' ||
	   c == '^' ||
	   c == '`' ||
	   c == '{' ||
	   c == '|' ||
	   c == '}' ) {
	    out += '%';
	    out += h[(c >> 4) & 0xf];
	    out += h[c & 0xf];
	} else {
	    out += char(c);
	}
    }
    return out;
}

string url_gpath(const string& url)
{
    // Remove the access schema part (or whatever it's called)
    string::size_type colon = url.find_first_of(":");
    if (colon == string::npos || colon == url.size() - 1)
        return url;
    // If there are non-alphanum chars before the ':', then there
    // probably is no scheme. Whatever...
    for (string::size_type i = 0; i < colon; i++) {
        if (!isalnum(url.at(i)))
            return url;
    }

    // In addition we canonize the path to remove empty host parts
    // (for compatibility with older versions of recoll where file://
    // was hardcoded, but the local path was used for doc
    // identification.
    return path_canon(url.substr(colon+1));
}

// Convert to file path if url is like file:
// Note: this only works with our internal pseudo-urls which are not
// encoded/escaped
string fileurltolocalpath(string url)
{
    if (url.find("file://") == 0)
        url = url.substr(7, string::npos);
    else
        return string();
    string::size_type pos;
    if ((pos = url.find_last_of("#")) != string::npos) {
        url.erase(pos);
    }
    return url;
}

// Printable url: this is used to transcode from the system charset
// into either utf-8 if transcoding succeeds, or url-encoded
bool printableUrl(const string &fcharset, const string &in, string &out)
{
    int ecnt = 0;
    if (!transcode(in, out, fcharset, "UTF-8", &ecnt) || ecnt) {
	out = url_encode(in, 7);
    }
    return true;
}


Pidfile::~Pidfile()
{
    if (m_fd >= 0)
	::close(m_fd);
    m_fd = -1;
}

pid_t Pidfile::read_pid()
{
    int fd = ::open(m_path.c_str(), O_RDONLY);
    if (fd == -1)
	return (pid_t)-1;

    char buf[16];
    int error;
    int i = read(fd, buf, sizeof(buf) - 1);
    error = errno;
    ::close(fd);
    if (i <= 0)
	return (pid_t)-1;
    buf[i] = '\0';
    char *endptr;
    pid_t pid = strtol(buf, &endptr, 10);
    if (endptr != &buf[i])
	return (pid_t)-1;
    return pid;
}

int Pidfile::flopen()
{
    const char *path = m_path.c_str();
    int operation = LOCK_EX | LOCK_NB;
    if ((m_fd = ::open(path, O_RDWR|O_CREAT, 0644)) == -1) {
	m_reason = "Open failed";
	return -1;
    }
    if (flock(m_fd, operation) == -1) {
	int serrno = errno;
	(void)::close(m_fd);
	errno = serrno;
	m_reason = "flock failed";
	return -1;
    }
    if (ftruncate(m_fd, 0) != 0) {
	/* can't happen [tm] */
	int serrno = errno;
	(void)::close(m_fd);
	errno = serrno;
	m_reason = "ftruncate failed";
	return -1;
    }
    return 0;
}

pid_t Pidfile::open()
{
    if (flopen() < 0) {
	return read_pid();
    }
    return (pid_t)0;
}

int Pidfile::write_pid()
{
    /* truncate to allow multiple calls */
    if (ftruncate(m_fd, 0) == -1) {
	m_reason = "ftruncate failed";
	return -1;
    }
    char pidstr[20];
    sprintf(pidstr, "%u", int(getpid()));
    lseek(m_fd, 0, 0);
    if (::write(m_fd, pidstr, strlen(pidstr)) != (ssize_t)strlen(pidstr)) {
	m_reason = "write failed";
	return -1;
    }
    return 0;
}

int Pidfile::close()
{
    return ::close(m_fd);
}

int Pidfile::remove()
{
    return unlink(m_path.c_str());
}

// Freedesktop standard paths for thumbnails

// Place for 256x256 files
static const string thmbdirlarge = ".thumbnails/large";
// 128x128
static const string thmbdirnormal = ".thumbnails/normal";

static void thumbname(const string& url, string& name)
{
    string digest;
    string l_url = url_encode(url);
    MD5String(l_url, digest);
    MD5HexPrint(digest, name);
    name += ".png";
}

bool thumbPathForUrl(const string& url, int size, string& path)
{
    string name;
    thumbname(url, name);
    if (size <= 128) {
	path = path_cat(path_home(), thmbdirnormal);
	path = path_cat(path, name);
	if (access(path.c_str(), R_OK) == 0) {
	    return true;
	}
    } 
    path = path_cat(path_home(), thmbdirlarge);
    path = path_cat(path, name);
    if (access(path.c_str(), R_OK) == 0) {
	return true;
    }

    // File does not exist. Path corresponds to the large version at this point,
    // fix it if needed.
    if (size <= 128) {
	path = path_cat(path_home(), thmbdirnormal);
	path = path_cat(path, name);
    }
    return false;
}

#else // TEST_PATHUT
#include <stdlib.h>
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
    vector<string>::const_iterator it;
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
    vector<string> matched = path_dirglob(dir, pattern);
    for (it = matched.begin(); it != matched.end();it++) {
	cout << *it << endl;
    }
#endif

#if 0
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

#if 0
  Pidfile pidfile("/tmp/pathutpidfile");
  pid_t pid;
  if ((pid = pidfile.open()) != 0) {
      cerr << "open failed. reason: " << pidfile.getreason() << 
	  " return " << pid << endl;
      exit(1);
  }
  pidfile.write_pid();
  sleep(10);
  pidfile.close();
  pidfile.remove();
#endif

#if 1
  if (argc != 2) {
      fprintf(stderr, "Usage: thumbpath <filepath> <size>\n");
      exit(1);
  }

  string input = *argv++;
  int size = atoi(*argv++);

  if (input.empty())  {
      fprintf(stderr, "Usage: thumbpath <filepath> <size>\n");
      exit(1);
  }

  // Make absolute path if needed
  if (input[0] != '/') {
      input = path_absolute(input);
  }
  input = string("file://") + path_canon(input);
  string path;
  if (thumbPathForUrl(input, size, path)) {
      cout << "Thumbnail for [" << input << "] [" << path << "]" << endl;
  } else {
      cout << "No thumbnail for [" << input << "] path [" << path << "]" <<endl;
  }
  
  exit(0);
#endif

    return 0;
}

#endif // TEST_PATHUT

