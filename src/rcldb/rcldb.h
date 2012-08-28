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
#ifndef _DB_H_INCLUDED_
#define _DB_H_INCLUDED_

#include <string>
#include <vector>

#include "cstr.h"
#include "refcntr.h"
#include "rcldoc.h"
#include "stoplist.h"
#include "rclconfig.h"

using std::string;
using std::vector;

// rcldb defines an interface for a 'real' text database. The current 
// implementation uses xapian only, and xapian-related code is in rcldb.cpp
// If support was added for other backend, the xapian code would be moved in 
// rclxapian.cpp, another file would be created for the new backend, and the
// configuration/compile/link code would be adjusted to allow choosing. There 
// is no plan for supporting multiple different backends.
// 
// In no case does this try to implement a useful virtualized text-db interface
// The main goal is simplicity and good matching to usage inside the recoll
// user interface. In other words, this is not exhaustive or well-designed or 
// reusable.
//
// Unique Document Identifier: uniquely identifies a document in its
// source storage (file system or other). Used for up to date checks
// etc. "udi". Our user is responsible for making sure it's not too
// big, cause it's stored as a Xapian term (< 150 bytes would be
// reasonable)

class RclConfig;

#ifndef NO_NAMESPACES
namespace Rcl {
#endif

// Omega compatible values. We leave a hole for future omega values. Not sure 
// it makes any sense to keep any level of omega compat given that the index
// is incompatible anyway.
enum value_slot {
    // Omega-compatible values:
    VALUE_LASTMOD = 0,	// 4 byte big endian value - seconds since 1970.
    VALUE_MD5 = 1,	// 16 byte MD5 checksum of original document.
    VALUE_SIZE = 2,     // sortable_serialise(<file size in bytes>)

    // Recoll only:
    VALUE_SIG = 10      // Doc sig as chosen by app (ex: mtime+size
};

class SearchData;
class TermIter;
class Query;

/** Used for returning result lists for index terms matching some criteria */
class TermMatchEntry {
public:
    TermMatchEntry() : wcf(0) {}
    TermMatchEntry(const string&t, int f, int d) : term(t), wcf(f), docs(d) {}
    TermMatchEntry(const string&t) : term(t), wcf(0) {}
    bool operator==(const TermMatchEntry &o) const { return term == o.term;}
    bool operator<(const TermMatchEntry &o) const { return term < o.term;}
    string term;
    int    wcf; // Total count of occurrences within collection.
    int    docs; // Number of documents countaining term.
};

class TermMatchResult {
public:
    TermMatchResult() {clear();}
    void clear() {entries.clear(); dbdoccount = 0; dbavgdoclen = 0;}
    vector<TermMatchEntry> entries;
    unsigned int dbdoccount;
    double       dbavgdoclen;
};

#ifdef IDX_THREADS
extern  void *DbUpdWorker(void*);
#endif // IDX_THREADS
/**
 * Wrapper class for the native database.
 */
class Db {
 public:
    // A place for things we don't want visible here.
    class Native;
    friend class Native;
#ifdef IDX_THREADS
    friend void *DbUpdWorker(void*);
#endif // IDX_THREADS

    /* General stuff (valid for query or update) ****************************/
    Db(RclConfig *cfp);
    ~Db();

    enum OpenMode {DbRO, DbUpd, DbTrunc};
    enum OpenError {DbOpenNoError, DbOpenMainDb, DbOpenExtraDb};
    bool open(OpenMode mode, OpenError *error = 0);
    bool close();
    bool isopen();

    /** Get explanation about last error */
    string getReason() const {return m_reason;}

    /** Return all possible stemmer names */
    static vector<string> getStemmerNames();

    /** Return existing stemming databases */
    vector<string> getStemLangs();

    /** Test word for spelling correction candidate: not too long, no 
	special chars... */
    static bool isSpellingCandidate(const string& term)
    {
	if (term.empty() || term.length() > 50)
	    return false;
	if (term.find_first_of(" !\"#$%&()*+,-./0123456789:;<=>?@[\\]^_`{|}~") 
	    != string::npos)
	    return false;
	return true;
    }


#ifdef TESTING_XAPIAN_SPELL
    /** Return spelling suggestion */
    string getSpellingSuggestion(const string& word);
#endif

    /* The next two, only for searchdata, should be somehow hidden */
    /* Return configured stop words */
    const StopList& getStopList() const {return m_stops;}
    /* Field name to prefix translation (ie: author -> 'A') */
    bool fieldToTraits(const string& fldname, const FieldTraits **ftpp);

    /* Update-related methods ******************************************/

    /** Test if the db entry for the given udi is up to date (by
     * comparing the input and stored sigs). 
     * Side-effect: set the existence flag for the file document 
     * and all subdocs if any (for later use by 'purge()') 
     */
    bool needUpdate(const string &udi, const string& sig);

    /** Add or update document. The Doc class should have been filled as much as
      * possible depending on the document type. parent_udi is only
      * use for subdocs, else set it to empty */
    bool addOrUpdate(const string &udi, const string &parent_udi, 
		     Doc &doc);

    /** Delete document(s) for given UDI, including subdocs */
    bool purgeFile(const string &udi, bool *existed = 0);

    /** Remove documents that no longer exist in the file system. This
     * depends on the update map, which is built during
     * indexing (needUpdate()). 
     *
     * This should only be called after a full walk of
     * the file system, else the update map will not be complete, and
     * many documents will be deleted that shouldn't, which is why this
     * has to be called externally, rcldb can't know if the indexing
     * pass was complete or partial.
     */
    bool purge();

    /** Create stem expansion database for given language. */
    bool createStemDbs(const std::vector<std::string> &langs);
    /** Delete stem expansion database for given language. */
    bool deleteStemDb(const string &lang);

    /* Query-related methods ************************************/

    /** Return total docs in db */
    int  docCnt(); 
    /** Return count of docs which have an occurrence of term */
    int termDocCnt(const string& term);
    /** Add extra database for querying */
    bool addQueryDb(const string &dir);
    /** Remove extra database. if dir == "", remove all. */
    bool rmQueryDb(const string &dir);
    /** Look where the doc result comes from. 
     * @return: 0 main index, (size_t)-1 don't know, 
     *   other: order of database in add_database() sequence.
     */
    size_t whatDbIdx(const Doc& doc);
    /** Tell if directory seems to hold xapian db */
    static bool testDbDir(const string &dir);

    /** Return the index terms that match the input string
     * Expansion is performed either with either wildcard or regexp processing
     * Stem expansion is performed if lang is not empty */
    enum MatchType {ET_WILD, ET_REGEXP, ET_STEM};
    bool termMatch(MatchType typ, const string &lang, const string &s, 
		   TermMatchResult& result, int max = -1, 
		   const string& field = cstr_null,
                   string *prefix = 0
        );
    /** Return min and max years for doc mod times in db */
    bool maxYearSpan(int *minyear, int *maxyear);

    /** Wildcard expansion specific to file names. Internal/sdata use only */
    bool filenameWildExp(const string& exp, vector<string>& names);

    /** Set parameters for synthetic abstract generation */
    void setAbstractParams(int idxTrunc, int synthLen, int syntCtxLen);

    /** Build synthetic abstract for document, extracting chunks relevant for
     * the input query. This uses index data only (no access to the file) */
    bool makeDocAbstract(Doc &doc, Query *query, string& abstract);
    bool makeDocAbstract(Doc &doc, Query *query, vector<string>& abstract);
    /** Retrieve detected page breaks positions */
    int getFirstMatchPage(Doc &doc, Query *query);

    /** Get document for given udi
     *
     * Used by the 'history' feature (and nothing else?) 
     */
    bool getDoc(const string &udi, Doc &doc);

    /* The following are mainly for the aspell module */
    /** Whole term list walking. */
    TermIter *termWalkOpen();
    bool termWalkNext(TermIter *, string &term);
    void termWalkClose(TermIter *);
    /** Test term existence */
    bool termExists(const string& term);
    /** Test if terms stem to different roots. */
    bool stemDiffers(const string& lang, const string& term, 
		     const string& base);

    RclConfig *getConf() {return m_config;}

    /** 
	Activate the "in place reset" mode where all documents are
	considered as needing update. This is a global/per-process
	option, and can't be reset. It should be set at the start of
	the indexing pass 
    */
    static void setInPlaceReset() {o_inPlaceReset = true;}

    /* This has to be public for access by embedded Query::Native */
    Native *m_ndb; 

private:
    // Internal form of close, can be called during destruction
    bool i_close(bool final);

    RclConfig *m_config;
    string     m_reason; // Error explanation

    /* Parameters cached out of the configuration files */
    // This is how long an abstract we keep or build from beginning of
    // text when indexing. It only has an influence on the size of the
    // db as we are free to shorten it again when displaying
    int          m_idxAbsTruncLen;
    // This is the size of the abstract that we synthetize out of query
    // term contexts at *query time*
    int          m_synthAbsLen;
    // This is how many words (context size) we keep around query terms
    // when building the abstract
    int          m_synthAbsWordCtxLen;
    // Flush threshold. Megabytes of text indexed before we flush.
    int          m_flushMb;
    // Text bytes indexed since beginning
    long long    m_curtxtsz;
    // Text bytes at last flush
    long long    m_flushtxtsz;
    // Text bytes at last fsoccup check
    long long    m_occtxtsz;
    // First fs occup check ?
    int         m_occFirstCheck;
    // Maximum file system occupation percentage
    int          m_maxFsOccupPc;
    // Database directory
    string       m_basedir;
    // Xapian directories for additional databases to query
    vector<string> m_extraDbs;
    OpenMode m_mode;
    // File existence vector: this is filled during the indexing pass. Any
    // document whose bit is not set at the end is purged
    vector<bool> updated;
    // Stop terms: those don't get indexed.
    StopList m_stops;
    // When this is set, all documents are considered as needing a reindex.
    // This implements an alternative to just erasing the index before 
    // beginning, with the advantage that, for small index formats updates, 
    // between releases the index remains available while being recreated.
    static bool o_inPlaceReset;

    // Reinitialize when adding/removing additional dbs
    bool adjustdbs(); 
    bool stemExpand(const string &lang, const string &s, 
		    TermMatchResult& result, int max = -1);

    // Flush when idxflushmb is reached
    bool maybeflush(off_t moretext);

    /* Copyconst and assignement private and forbidden */
    Db(const Db &) {}
    Db& operator=(const Db &) {return *this;};
};

// This has to go somewhere, and as it needs the Xapian version, this is
// the most reasonable place.
string version_string();

extern const string pathelt_prefix;
extern const string start_of_field_term;
extern const string end_of_field_term;
extern const string page_break_term;

}

#endif /* _DB_H_INCLUDED_ */
