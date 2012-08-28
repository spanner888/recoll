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
#ifndef _STEMDB_H_INCLUDED_
#define _STEMDB_H_INCLUDED_

/** Stem database code
 * 
 * Stem databases list stems and the set of index terms they expand to. They 
 * are computed from index data by stemming each term and regrouping those 
 * that stem to the same value.
 *
 * Stem databases are stored as separate Xapian databases, in
 * subdirectories of the index (e.g.: stem_french, stem_german2)
 *
 * The stem database is generated at the end of an indexing session by
 * walking the whole index term list, computing the stem for each
 * term, and building a stem->terms map.
 * 
 * The map is then stored as a Xapian index where each stem is the
 * unique term indexing a document, and the list of expansions is stored
 * as the document data record. It would probably be possible to store
 * the expansions as the document term list instead (using a prefix to
 * distinguish the stem term). I tried this (chert, 08-2012) and the stem
 * db creation is very slightly slower than with the record approach, and
 * the result is 50% bigger.
 *
 * Another possible approach would be to update the stem map as we index. 
 * This would probably be be less efficient for a full index pass because
 * each term would be seen and stemmed many times, but it might be
 * more efficient for an incremental pass with a limited number of
 * updated documents. For a small update, the stem building part often
 * dominates the indexing time.
 * 
 * For future reference, I did try to store the map in a gdbm file and
 * the result is bigger and takes more time to create than the Xapian version.
 */

#include <vector>
#include <string>

#include <xapian.h>

#include "synfamily.h"

namespace Rcl {

class StemDb : public XapSynFamily {
public:
    StemDb(Xapian::Database& xdb)
	: XapSynFamily(xdb, synFamStem)
    {
    }

    /** Expand for a number of languages */
    bool stemExpand(const std::string& langs,
		     const std::string& term,
		     std::vector<std::string>& result);
private:
    /** Compute stem and call synExpand() */
    bool expandOne(const std::string& lang,
		   const std::string& term,
		   std::vector<std::string>& result);
};

extern bool createExpansionDbs(Xapian::WritableDatabase& wdb, 
			       const std::vector<std::string>& langs);

}

#endif /* _STEMDB_H_INCLUDED_ */
