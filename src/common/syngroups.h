/* Copyright (C) 2015 J.F.Dockes
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

#ifndef _SYNGROUPS_H_INCLUDED_
#define _SYNGROUPS_H_INCLUDED_

#include <string>
#include <vector>

// Manage synonym groups. This is very different from stemming and
// case/diac expansion because there is no reference form: all terms
// in a group are equivalent.
class SynGroups {
public:
    SynGroups(const std::string& fname);
    ~SynGroups();
    std::vector<std::string> getgroup(const std::string& term);
    bool ok();
private:
    class Internal;
    Internal *m;
    SynGroups(const SynGroups&);
    SynGroups& operator=(const SynGroups&);
};

#endif /* _SYNGROUPS_H_INCLUDED_ */
