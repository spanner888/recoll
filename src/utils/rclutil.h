/* Copyright (C) 2016 J.F.Dockes
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

#ifndef _RCLUTIL_H_INCLUDED_
#define _RCLUTIL_H_INCLUDED_
#include "autoconfig.h"

// Misc stuff not generic enough to get into smallut or pathut

#include <map>
#include <string>
#include MEMORY_INCLUDE


extern void rclutil_init_mt();

/// Sub-directory for default recoll config (e.g: .recoll)
extern std::string path_defaultrecollconfsubdir();

/// e.g. /usr/share/recoll. Depends on OS and config
extern const std::string& path_pkgdatadir();

/// Transcode to utf-8 if possible or url encoding, for display.
extern bool printableUrl(const std::string& fcharset,
                         const std::string& in, std::string& out);
/// Same but, in the case of a Windows local path, also turn "c:/" into
/// "/c/" This should be used only for splitting the path in rcldb.
extern std::string url_gpathS(const std::string& url);

/// Retrieve the temp dir location: $RECOLL_TMPDIR else $TMPDIR else /tmp
extern const std::string& tmplocation();

/// Create temporary directory (inside the temp location)
extern bool maketmpdir(std::string& tdir, std::string& reason);

/// Temporary file class
class TempFileInternal {
public:
    TempFileInternal(const std::string& suffix);
    ~TempFileInternal();
    const char *filename() {
        return m_filename.c_str();
    }
    const std::string& getreason() {
        return m_reason;
    }
    void setnoremove(bool onoff) {
        m_noremove = onoff;
    }
    bool ok() {
        return !m_filename.empty();
    }
private:
    std::string m_filename;
    std::string m_reason;
    bool m_noremove;
};

typedef STD_SHARED_PTR<TempFileInternal> TempFile;

/// Temporary directory class. Recursively deleted by destructor.
class TempDir {
public:
    TempDir();
    ~TempDir();
    const char *dirname() {
        return m_dirname.c_str();
    }
    const std::string& getreason() {
        return m_reason;
    }
    bool ok() {
        return !m_dirname.empty();
    }
    /// Recursively delete contents but not self.
    bool wipe();
private:
    std::string m_dirname;
    std::string m_reason;
    TempDir(const TempDir&) {}
    TempDir& operator=(const TempDir&) {
        return *this;
    };
};

// Freedesktop thumbnail standard path routine
// On return, path will have the appropriate value in all cases,
// returns true if the file already exists
extern bool thumbPathForUrl(const std::string& url, int size,
                            std::string& path);

// Duplicate map<string,string> while ensuring no shared string data (to pass
// to other thread):
void map_ss_cp_noshr(const std::map<std::string, std::string> s,
                     std::map<std::string, std::string> *d);


#endif /* _RCLUTIL_H_INCLUDED_ */
