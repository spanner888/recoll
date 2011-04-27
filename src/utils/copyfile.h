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
#ifndef _COPYFILE_H_INCLUDED_
#define _COPYFILE_H_INCLUDED_

#include <string>
using std::string;

enum CopyfileFlags {COPYFILE_NONE = 0, COPYFILE_NOERRUNLINK = 1};

extern bool copyfile(const char *src, const char *dst, string &reason,
		     int flags = 0);

// Try to rename, copy/unlink source if this fails (different devs)
extern bool renameormove(const char *src, const char *dst, string &reason);

#endif /* _COPYFILE_H_INCLUDED_ */
