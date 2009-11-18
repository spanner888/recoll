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
#ifndef lint
static char rcsid[] = "@(#$Id: $ (C) 2009 J.F.Dockes";
#endif

#ifndef TEST_CIRCACHE

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <memory.h>
#include <zlib.h>

#include <sstream>
#include <iostream>
#include <map>

#include "circache.h"
#include "conftree.h"
#include "debuglog.h"
#include "smallut.h"
#include "md5.h"

using namespace std;
typedef unsigned char UCHAR;
typedef unsigned int UINT;
typedef unsigned long ULONG;

static bool inflateToDynBuf(void *inp, UINT inlen, void **outpp, UINT *outlenp);

/*
 * File structure:
 * - Starts with a 1-KB header block, with a param dictionary.
 * - Stored items follow. Each item has a header and 2 segments for
 *   the metadata and the data. 
 *   The segment sizes are stored in the ascii header/marker:
 *     circacheSizes = xxx yyy zzz
 *     xxx bytes of metadata
 *     yyy bytes of data
 *     zzz bytes of padding up to next object (only one entry has non zero)
 *
 * There is a write position, which can be at eof while 
 * the file is growing, or inside the file if we are recycling. This is stored
 * in the header (oheadoffs), together with the maximum size
 *
 * If we are recycling, we have to take care to compute the size of the 
 * possible remaining area from the last object invalidated by the write, 
 * pad it with neutral data and store the size in the new header. To help with
 * this, the address for the last object written is also kept in the header
 * (nheadoffs, npadsize)
 * 
 */

// First block size
#define CIRCACHE_FIRSTBLOCK_SIZE 1024

// Entry header.
// 3 x 32 bits sizes as hex integers + 1 x 16 bits flag + at least 1 zero
//                          15 +             3x(9)  + 3 + 1 = 46
const char *headerformat = "circacheSizes = %x %x %x %hx";
#define CIRCACHE_HEADER_SIZE 64

class EntryHeaderData {
public:
    EntryHeaderData() : dicsize(0), datasize(0), padsize(0), flags(0) {}
    UINT dicsize;
    UINT datasize;
    UINT padsize;
    unsigned short flags;
};
enum EntryFlags {EFNone = 0, EFDataCompressed = 1};

// A callback class for the header-hopping function.
class CCScanHook {
public:
    virtual ~CCScanHook() {}
    enum status {Stop, Continue, Error, Eof};
    virtual status takeone(off_t offs, const string& udi, 
                           const EntryHeaderData& d) = 0;
};

// We have an auxiliary in-memory multimap of hashed-udi -> offset to
// speed things up. This is created the first time the file is scanned
// (on the first get), and not saved to disk. 

// The map key: hashed udi. As a very short hash seems sufficient,
// maybe we could find something faster/simpler than md5?
#define UDIHLEN 4
class UdiH {
public:
    UCHAR h[UDIHLEN];

    UdiH(const string& udi)
    {
        MD5_CTX ctx;
        MD5Init(&ctx);
        MD5Update(&ctx, (const UCHAR*)udi.c_str(), udi.length());
        UCHAR md[16];
        MD5Final(md, &ctx);
        memcpy(h, md, UDIHLEN);
    }

    string asHexString() const {
        static const char hex[]="0123456789abcdef";
        string out;
        for (int i = 0; i < UDIHLEN; i++) {
            out.append(1, hex[h[i] >> 4]);
            out.append(1, hex[h[i] & 0x0f]);
        }
        return out;
    }
    bool operator==(const UdiH& r) const
    {
        for (int i = 0; i < UDIHLEN; i++)
            if (h[i] != r.h[i])
                return false;
        return true;
    }
    bool operator<(const UdiH& r) const
    {
        for (int i = 0; i < UDIHLEN; i++) {
            if (h[i] < r.h[i])
                return true;
            if (h[i] > r.h[i])
                return false;
        }
        return false;
    }
};
typedef multimap<UdiH, off_t> kh_type;
typedef multimap<UdiH, off_t>::value_type kh_value_type;

class CirCacheInternal {
public:
    int m_fd;
    ////// These are cache persistent state and written to the first block:
    // Maximum file size, after which we begin reusing old space
    off_t m_maxsize; 
    // Offset of the oldest header. 
    off_t m_oheadoffs;
    // Offset of last write (newest header)
    off_t m_nheadoffs;
    // Pad size for newest entry. 
    int   m_npadsize;
    ///////////////////// End header entries

    // A place to hold data when reading
    char  *m_buffer;
    size_t m_bufsiz;

    // Error messages
    ostringstream m_reason;

    // State for rewind/next/getcurrent operation. This could/should
    // be moved to a separate iterator.
    off_t  m_itoffs;
    EntryHeaderData m_ithd;

    // Offset cache
    kh_type m_ofskh;
    bool    m_ofskhcplt; // Has cache been fully read since open?

    // Add udi->offset translation to map
    bool khEnter(const string& udi, off_t ofs)
    {
        UdiH h(udi);

        LOGDEB2(("Circache::khEnter: h %s offs %lu udi [%s]\n", 
                h.asHexString().c_str(), (ULONG)ofs, udi.c_str()));

        pair<kh_type::iterator, kh_type::iterator> p = m_ofskh.equal_range(h);

        if (p.first != m_ofskh.end() && p.first->first == h) {
            for (kh_type::iterator it = p.first; it != p.second; it++) {
                LOGDEB2(("Circache::khEnter: col h %s, ofs %lu\n", 
                        it->first.asHexString().c_str(),
                        (ULONG)it->second));
                if (it->second == ofs) {
                    // (h,offs) already there. Happens
                    LOGDEB2(("Circache::khEnter: already there\n"));
                    return true;
                }
            }
        }
        m_ofskh.insert(kh_value_type(h, ofs));
        LOGDEB2(("Circache::khEnter: inserted\n"));
        return true;
    }
    void khDump()
    {
        for (kh_type::const_iterator it = m_ofskh.begin(); 
             it != m_ofskh.end(); it++) {
            LOGDEB(("Circache::KHDUMP: %s %d\n", 
                    it->first.asHexString().c_str(), (ULONG)it->second));
        }
    }
    bool khFind(const string& udi, vector<off_t>& ofss)
    {
        ofss.clear();

        UdiH h(udi);

        LOGDEB2(("Circache::khFind: h %s udi [%s]\n", 
                h.asHexString().c_str(), udi.c_str()));

        pair<kh_type::iterator, kh_type::iterator> p = 
            m_ofskh.equal_range(h);

#if 0
        if (p.first == m_ofskh.end()) LOGDEB(("KHFIND: FIRST END()\n"));
        if (p.second == m_ofskh.end()) LOGDEB(("KHFIND: SECOND END()\n"));
        if (!(p.first->first == h))
            LOGDEB(("KHFIND: NOKEY: %s %s\n", 
                    p.first->first.asHexString().c_str(),
                    p.second->first.asHexString().c_str()));
#endif 

        if (p.first == m_ofskh.end() || !(p.first->first == h))
            return false;

        for (kh_type::iterator it = p.first; it != p.second; it++) {
            ofss.push_back(it->second);
        }
        return true;
    }

    CirCacheInternal()
        : m_fd(-1), m_maxsize(-1), m_oheadoffs(-1), 
          m_nheadoffs(0), m_npadsize(0), m_buffer(0), m_bufsiz(0), 
          m_ofskhcplt(false)
    {}

    ~CirCacheInternal()
    {
        if (m_fd >= 0)
            close(m_fd);
        if (m_buffer)
            free(m_buffer);
    }

    char *buf(size_t sz)
    {
        if (m_bufsiz >= sz)
            return m_buffer;
        if ((m_buffer = (char *)realloc(m_buffer, sz))) {
            m_bufsiz = sz;
        } else {
            m_reason << "CirCache:: realloc(" << sz << ") failed";
            m_bufsiz = 0;
        }
        return m_buffer;
    }

    // Name for the cache file
    string datafn(const string& d)
    {
        return  path_cat(d, "circache.crch");
    }

    bool writefirstblock()
    {
        assert(m_fd >= 0);

        ostringstream s;
        s << 
            "maxsize = " << m_maxsize << "\n" <<
            "oheadoffs = " << m_oheadoffs << "\n" <<
            "nheadoffs = " << m_nheadoffs << "\n" <<
            "npadsize = " << m_npadsize << "\n" <<
            "                                                              " <<
            "                                                              " <<
            "                                                              " <<
            "\0";

        int sz = int(s.str().size());
        assert(sz < CIRCACHE_FIRSTBLOCK_SIZE);
        lseek(m_fd, 0, 0);
        if (write(m_fd, s.str().c_str(), sz) != sz) {
            m_reason << "writefirstblock: write() failed: errno " << errno;
            return false;
        }
        return true;
    }

    bool readfirstblock()
    {
        assert(m_fd >= 0);

        char *bf = buf(CIRCACHE_FIRSTBLOCK_SIZE);
        if (!bf)
            return false;
        lseek(m_fd, 0, 0);
        if (read(m_fd, bf, CIRCACHE_FIRSTBLOCK_SIZE) != 
            CIRCACHE_FIRSTBLOCK_SIZE) {
            m_reason << "readfirstblock: read() failed: errno " << errno;
            return false;
        }
        string s(bf, CIRCACHE_FIRSTBLOCK_SIZE);
        ConfSimple conf(s, 1);
        string value;
        if (!conf.get("maxsize", value, "")) {
            m_reason << "readfirstblock: conf get maxsize failed";
            return false;
        }
        m_maxsize = atol(value.c_str());
        if (!conf.get("oheadoffs", value, "")) {
            m_reason << "readfirstblock: conf get oheadoffs failed";
            return false;
        }
        m_oheadoffs = atol(value.c_str());
        if (!conf.get("nheadoffs", value, "")) {
            m_reason << "readfirstblock: conf get nheadoffs failed";
            return false;
        }
        m_nheadoffs = atol(value.c_str());
        if (!conf.get("npadsize", value, "")) {
            m_reason << "readfirstblock: conf get npadsize failed";
            return false;
        }
        m_npadsize = atol(value.c_str());
        return true;
    }            

    bool writeentryheader(off_t offset, const EntryHeaderData& d)
    {
        char *bf = buf(CIRCACHE_HEADER_SIZE);
        if (bf == 0)
            return false;
        memset(bf, 0, CIRCACHE_HEADER_SIZE);
        sprintf(bf, headerformat, d.dicsize, d.datasize, d.padsize, d.flags);
        if (lseek(m_fd, offset, 0) != offset) {
            m_reason << "CirCache::weh: lseek(" << offset << 
                ") failed: errno " << errno;
            return false;
        }
        if (write(m_fd, bf, CIRCACHE_HEADER_SIZE) !=  CIRCACHE_HEADER_SIZE) {
            m_reason << "CirCache::weh: write failed. errno " << errno;
            return false;
        }
        return true;
    }

    CCScanHook::status readentryheader(off_t offset, EntryHeaderData& d)
    {
        assert(m_fd >= 0);

        if (lseek(m_fd, offset, 0) != offset) {
            m_reason << "readentryheader: lseek(" << offset << 
                ") failed: errno " << errno;
            return CCScanHook::Error;
        }
        char *bf = buf(CIRCACHE_HEADER_SIZE);
        if (bf == 0) {
            return CCScanHook::Error;
        }
        int ret = read(m_fd, bf, CIRCACHE_HEADER_SIZE);
        if (ret == 0) {
            // Eof
            m_reason << " Eof ";
            return CCScanHook::Eof;
        }
        if (ret != CIRCACHE_HEADER_SIZE) {
            m_reason << " readheader: read failed errno " << errno;
            return CCScanHook::Error;
        }
        if (sscanf(bf, headerformat, &d.dicsize, &d.datasize, 
                   &d.padsize, &d.flags) != 4) {
            m_reason << " readentryheader: bad header at " << 
                offset << " [" << bf << "]";
            return CCScanHook::Error;
        }
        LOGDEB2(("Circache:readentryheader: dcsz %u dtsz %u pdsz %u flgs %hu\n",
                 d.dicsize, d.datasize, d.padsize, d.flags));
        return CCScanHook::Continue;
    }

    CCScanHook::status scan(off_t startoffset, CCScanHook *user, 
                            bool fold = false)
    {
        assert(m_fd >= 0);

        off_t so0 = startoffset;
        bool already_folded = false;
        
        while (true) {
            if (already_folded && startoffset == so0) {
                m_ofskhcplt = true;
                return CCScanHook::Eof;
            }

            EntryHeaderData d;
            CCScanHook::status st;
            switch ((st = readentryheader(startoffset, d))) {
            case CCScanHook::Continue: break;
            case CCScanHook::Eof:
                if (fold && !already_folded) {
                    already_folded = true;
                    startoffset = CIRCACHE_FIRSTBLOCK_SIZE;
                    continue;
                }
                /* FALLTHROUGH */
            default:
                return st;
            }

            char *bf;
            if ((bf = buf(d.dicsize+1)) == 0) {
                return CCScanHook::Error;
            }
            bf[d.dicsize] = 0;
            if (read(m_fd, bf, d.dicsize) != int(d.dicsize)) {
                m_reason << "scan: read failed errno " << errno;
                return CCScanHook::Error;
            }
            string b(bf, d.dicsize);
            ConfSimple conf(b, 1);
            
            string udi;
            if (!conf.get("udi", udi, "")) {
                m_reason << "scan: no udi in dic";
                return CCScanHook::Error;
            }
            khEnter(udi, startoffset);

            // Call callback
            CCScanHook::status a = 
                user->takeone(startoffset, udi, d);
            switch (a) {
            case CCScanHook::Continue: 
                break;
            default:
                return a;
            }
            startoffset += CIRCACHE_HEADER_SIZE + d.dicsize + 
                d.datasize + d.padsize;
        }
    }

    bool readHDicData(off_t hoffs, EntryHeaderData& d, string& dic, 
                      string* data)
    {
        if (readentryheader(hoffs, d) != CCScanHook::Continue)
            return false;
        return readDicData(hoffs, d, dic, data);
    }

    bool readDicData(off_t hoffs, EntryHeaderData& hd, string& dic, 
                     string* data)
    {
        off_t offs = hoffs + CIRCACHE_HEADER_SIZE;
        // This syscall could be avoided in some cases if we saved the offset
        // at each seek. In most cases, we just read the header and we are
        // at the right position
        if (lseek(m_fd, offs, 0) != offs) {
            m_reason << "CirCache::get: lseek(" << offs << ") failed: " << 
                errno;
            return false;
        }
        char *bf = buf(hd.dicsize);
        if (bf == 0)
            return false;
        if (read(m_fd, bf, hd.dicsize) != int(hd.dicsize)) {
            m_reason << "CirCache::get: read() failed: errno " << errno;
            return false;
        }
        dic.assign(bf, hd.dicsize);

        if (data == 0)
            return true;

        bf = buf(hd.datasize);
        if (bf == 0)
            return false;
        if (read(m_fd, bf, hd.datasize) != int(hd.datasize)){
            m_reason << "CirCache::get: read() failed: errno " << errno;
            return false;
        }

        if (hd.flags & EFDataCompressed) {
            LOGDEB1(("Circache:readdicdata: data compressed\n"));
            char *uncomp;
            unsigned int uncompsize;
            if (!inflateToDynBuf(bf, hd.datasize, 
                                 (void **)&uncomp, &uncompsize)) {
                m_reason << "CirCache: decompression failed ";
                return false;
            }
            data->assign(uncomp, uncompsize);
            free(uncomp);
        } else {
            LOGDEB1(("Circache:readdicdata: data NOT compressed\n"));
            data->assign(bf, hd.datasize);
        }

        return true;
    }

};

CirCache::CirCache(const string& dir)
    : m_dir(dir)
{
    m_d = new CirCacheInternal;
    LOGDEB0(("CirCache: [%s]\n", m_dir.c_str()));
}

CirCache::~CirCache()
{
    delete m_d;
    m_d = 0;
}

string CirCache::getReason()
{
    return m_d ? m_d->m_reason.str() : "Not initialized";
}

bool CirCache::create(off_t m_maxsize, bool onlyifnotexists)
{
    LOGDEB(("CirCache::create: [%s]\n", m_dir.c_str()));
    assert(m_d != 0);
    struct stat st;
    if (stat(m_dir.c_str(), &st) < 0) {
        if (mkdir(m_dir.c_str(), 0777) < 0) {
            m_d->m_reason << "CirCache::create: mkdir(" << m_dir << 
                ") failed" << " errno " << errno;
            return false;
        }
    } else {
        if (onlyifnotexists)
            return open(CC_OPWRITE);
    }

    if ((m_d->m_fd = ::open(m_d->datafn(m_dir).c_str(), 
                          O_CREAT | O_RDWR | O_TRUNC, 
                          0666)) < 0) {
        m_d->m_reason << "CirCache::create: open/creat(" << 
            m_d->datafn(m_dir) << ") failed " << "errno " << errno;
            return false;
    }

    m_d->m_maxsize = m_maxsize;
    m_d->m_oheadoffs = CIRCACHE_FIRSTBLOCK_SIZE;

    char buf[CIRCACHE_FIRSTBLOCK_SIZE];
    memset(buf, 0, CIRCACHE_FIRSTBLOCK_SIZE);
    if (::write(m_d->m_fd, buf, CIRCACHE_FIRSTBLOCK_SIZE) != 
        CIRCACHE_FIRSTBLOCK_SIZE) {
        m_d->m_reason << "CirCache::create: write header failed, errno " 
                      << errno;
        return false;
    }
    return m_d->writefirstblock();
}

bool CirCache::open(OpMode mode)
{
    assert(m_d != 0);
    if (m_d->m_fd >= 0)
        ::close(m_d->m_fd);

    if ((m_d->m_fd = ::open(m_d->datafn(m_dir).c_str(), 
                          mode == CC_OPREAD ? O_RDONLY : O_RDWR)) < 0) {
        m_d->m_reason << "CirCache::open: open(" << m_d->datafn(m_dir) << 
            ") failed " << "errno " << errno;
        return false;
    }
    return m_d->readfirstblock();
}

class CCScanHookDump : public  CCScanHook {
public:
    virtual status takeone(off_t offs, const string& udi, 
                           const EntryHeaderData& d)
    {
        cout << "Scan: offs " << offs << " dicsize " << d.dicsize 
             << " datasize " << d.datasize << " padsize " << d.padsize << 
            " flags " << d.flags <<
            " udi [" << udi << "]" << endl;
        return Continue;
    }
};

bool CirCache::dump()
{
    CCScanHookDump dumper;

    // Start at oldest header. This is eof while the file is growing, scan will
    // fold to bot at once.
    off_t start = m_d->m_oheadoffs;

    switch (m_d->scan(start, &dumper, true)) {
    case CCScanHook::Stop: 
        cout << "Scan returns Stop??" << endl;
        return false;
    case CCScanHook::Continue: 
        cout << "Scan returns Continue ?? " << CCScanHook::Continue << " " <<
            getReason() << endl;
        return false;
    case CCScanHook::Error: 
        cout << "Scan returns Error: " << getReason() << endl;
        return false;
    case CCScanHook::Eof:
        cout << "Scan returns Eof (ok)" << endl;
        return true;
    default:
        cout << "Scan returns Unknown ??" << endl;
        return false;
    }
}

class CCScanHookGetter : public  CCScanHook {
public:
    string  m_udi;
    int     m_targinstance;
    int     m_instance;
    off_t   m_offs;
    EntryHeaderData m_hd;

    CCScanHookGetter(const string &udi, int ti)
        : m_udi(udi), m_targinstance(ti), m_instance(0), m_offs(0){}

    virtual status takeone(off_t offs, const string& udi, 
                           const EntryHeaderData& d)
    {
        LOGDEB2(("Circache:Scan: off %ld udi [%s] dcsz %u dtsz %u pdsz %u "
                 " flgs %hu\n",
                 long(offs), udi.c_str(), (UINT)d.dicsize, 
                 (UINT)d.datasize, (UINT)d.padsize, d.flags));
        if (!m_udi.compare(udi)) {
            m_instance++;
            m_offs = offs;
            m_hd = d;
            if (m_instance == m_targinstance)
                return Stop;
        }
        return Continue;
    }
};

// instance == -1 means get latest. Otherwise specify from 1+
bool CirCache::get(const string& udi, string& dic, string& data, int instance)
{
    Chrono chron;

    assert(m_d != 0);
    if (m_d->m_fd < 0) {
        m_d->m_reason << "CirCache::get: not open";
        return false;
    }

    LOGDEB0(("CirCache::get: udi [%s], instance %d\n", udi.c_str(), instance));

    // If memory map is up to date, use it:
    if (m_d->m_ofskhcplt) {
        LOGDEB1(("CirCache::get: using ofskh\n"));
        //m_d->khDump();
        vector<off_t> ofss;
        if (m_d->khFind(udi, ofss)) {
            LOGDEB1(("Circache::get: h found, colls %d\n", ofss.size()));
            int finst = 1;
            EntryHeaderData d_good;
            off_t           o_good = 0;
            for (vector<off_t>::iterator it = ofss.begin();
                 it != ofss.end(); it++) {
                LOGDEB1(("Circache::get: trying offs %lu\n", (ULONG)*it));
                string fdic;
                EntryHeaderData d;
                if (!m_d->readHDicData(*it, d, fdic, 0)) 
                    return false;
                ConfSimple conf(fdic);
                string fudi;
                if (!conf.get("udi", fudi, "")) {
                    m_d->m_reason << "get: bad file: no udi in dic";
                    return false;
                }
                if (!fudi.compare(udi)) {
                    // Found one, memorize offset. Done if instance
                    // matches, else go on. If instance is -1 need to
                    // go to the end of the list anyway
                    d_good = d;
                    o_good = *it;
                    if (finst == instance) {
                        break;
                    } else {
                        finst++;
                    }
                }
            }
            // Did we read an appropriate entry ?
            if (o_good != 0 && (instance == -1 || instance == finst)) {
                bool ret = m_d->readDicData(o_good, d_good, dic, &data);
                LOGDEB0(("Circache::get: hfound, %d mS\n", 
                         chron.millis()));
                return ret;
            }
            // Else try to scan anyway.
        }
    }

    CCScanHookGetter getter(udi, instance);
    off_t start = m_d->m_oheadoffs;

    CCScanHook::status ret = m_d->scan(start, &getter, true);
    if (ret == CCScanHook::Eof) {
        if (getter.m_instance == 0)
            return false;
    } else if (ret != CCScanHook::Stop) {
        return false;
    }
    bool bret = 
        m_d->readDicData(getter.m_offs, getter.m_hd, dic, &data);
    LOGDEB0(("Circache::get: scanfound, %d mS\n", chron.millis()));

    return bret;
}

// Used to scan the file ahead until we accumulated enough space for the new
// entry. 
class CCScanHookSpacer : public  CCScanHook {
public:
    UINT sizewanted;
    UINT sizeseen;
    
    CCScanHookSpacer(int sz)
        : sizewanted(sz), sizeseen(0) {assert(sz > 0);}

    virtual status takeone(off_t offs, const string& udi, 
                           const EntryHeaderData& d)
    {
        LOGDEB2(("Circache:ScanSpacer:off %u dcsz %u dtsz %u pdsz %u udi[%s]\n",
                (UINT)offs, d.dicsize, d.datasize, d.padsize, udi.c_str()));
        sizeseen += CIRCACHE_HEADER_SIZE + d.dicsize + d.datasize + d.padsize;
        if (sizeseen >= sizewanted)
            return Stop;
        return Continue;
    }
};

bool CirCache::put(const string& udi, const ConfSimple *iconf, 
                   const string& data, unsigned int iflags)
{
    assert(m_d != 0);
    if (m_d->m_fd < 0) {
        m_d->m_reason << "CirCache::put: not open";
        return false;
    }

    // We want udi in metadata
    string dic;
    if (!iconf || !iconf->get("udi", dic) || dic.empty() || dic.compare(udi)) {
        m_d->m_reason << "No/bad 'udi' entry in input dic";
        LOGERR(("Circache::put: no/bad udi: DIC:[%s] UDI [%s]\n", 
                dic.c_str(), udi.c_str()));
        iconf->write(cerr);
        return false;
    }
    ostringstream s;
    iconf->write(s);
    dic = s.str();

    // Data compression ?
    const char *datap = data.c_str();
    unsigned int datalen = data.size();
    unsigned short flags = 0;
    if (!(iflags & NoCompHint)) {
        ULONG len = compressBound(data.size());
        char *bf = m_d->buf(len);
        if (bf != 0 && 
            compress((Bytef*)bf, &len, (Bytef*)data.c_str(), data.size()) 
            == Z_OK) {
            if (float(len) < 0.9 * float(data.size())) {
                // bf is local but it's our static buffer address
                datap = bf;
                datalen = len;
                flags |= EFDataCompressed;
            }
        }
    }

    struct stat st;
    if (fstat(m_d->m_fd, &st) < 0) {
        m_d->m_reason << "CirCache::put: fstat failed. errno " << errno;
        return false;
    }

    // Characteristics for the new entry
    int nsize = CIRCACHE_HEADER_SIZE + dic.size() + datalen;
    int nwriteoffs = 0;
    int npadsize = 0;
    bool extending = false;

    LOGDEB2(("CirCache::put: nsz %d oheadoffs %d\n", nsize, m_d->m_oheadoffs));

    if (st.st_size < m_d->m_maxsize) {
        // If we are still growing the file, things are simple
        nwriteoffs = lseek(m_d->m_fd, 0, SEEK_END);
        npadsize = 0;
        extending = true;
    } else {
        // We'll write at the oldest header, minus the possible
        // padsize for the previous (latest) one.
        int recovpadsize = m_d->m_oheadoffs == CIRCACHE_FIRSTBLOCK_SIZE ?
            0 : m_d->m_npadsize;
        if (recovpadsize == 0) {
            // No padsize to recover
            nwriteoffs = m_d->m_oheadoffs;
        } else {
            // Need to read the latest entry's header, to rewrite it with a 
            // zero pad size
            EntryHeaderData pd;
            if (m_d->readentryheader(m_d->m_nheadoffs, pd) != 
                CCScanHook::Continue) {
                return false;
            }
            assert(int(pd.padsize) == m_d->m_npadsize);
            LOGDEB2(("CirCache::put: recovering previous padsize %d\n",
                     pd.padsize));
            pd.padsize = 0;
            if (!m_d->writeentryheader(m_d->m_nheadoffs, pd)) {
                return false;
            }
            nwriteoffs = m_d->m_oheadoffs - recovpadsize;
            // If we fail between here and the end, the file is hosed.
        }

        if (nsize <= recovpadsize) {
            // If the new entry fits entirely in the pad area from the
            // latest one, no need to recycle the oldest entries.
            LOGDEB2(("CirCache::put: new fits in old padsize %d\n,"
                     recovpadsize));
            npadsize = recovpadsize - nsize;
        } else {
            // Scan the file until we have enough space for the new entry,
            // and determine the pad size up to the 1st preserved entry
            int scansize = nsize - recovpadsize;
            LOGDEB2(("CirCache::put: scanning for size %d from offs %u\n",
                    scansize, (UINT)m_d->m_oheadoffs));
            CCScanHookSpacer spacer(scansize);
            switch (m_d->scan(m_d->m_oheadoffs, &spacer)) {
            case CCScanHook::Stop: 
                LOGDEB2(("CirCache::put: Scan ok, sizeseen %d\n", 
                         spacer.sizeseen));
                npadsize = spacer.sizeseen - scansize;
                break;
            case CCScanHook::Eof:
                // npadsize is 0
                extending = true;
                break;
            case CCScanHook::Continue: 
            case CCScanHook::Error: 
                return false;
            }
        }
    }
    
    LOGDEB2(("CirCache::put: writing %d at %d padsize %d\n", 
             nsize, nwriteoffs, npadsize));
    if (lseek(m_d->m_fd, nwriteoffs, 0) != nwriteoffs) {
        m_d->m_reason << "CirCache::put: lseek failed: " << errno;
        return false;
    }
    char *bf = m_d->buf(CIRCACHE_HEADER_SIZE);
    if (bf == 0) 
        return false;
    char head[CIRCACHE_HEADER_SIZE];
    memset(head, 0, CIRCACHE_HEADER_SIZE);
    sprintf(head, headerformat, dic.size(), datalen, npadsize, flags);
    struct iovec vecs[3];
    vecs[0].iov_base = head;
    vecs[0].iov_len = CIRCACHE_HEADER_SIZE;
    vecs[1].iov_base = (void *)dic.c_str();
    vecs[1].iov_len = dic.size();
    vecs[2].iov_base = (void *)datap;
    vecs[2].iov_len = datalen;
    if (writev(m_d->m_fd, vecs, 3) !=  nsize) {
        m_d->m_reason << "put: write failed. errno " << errno;
        if (extending)
            ftruncate(m_d->m_fd, m_d->m_oheadoffs);
        return false;
    }

    m_d->khEnter(udi, nwriteoffs);

    // Update first block information
    m_d->m_nheadoffs = nwriteoffs;
    m_d->m_npadsize  = npadsize;
    // New oldest header is the one just after the one we just wrote.
    m_d->m_oheadoffs = nwriteoffs + nsize + npadsize;
    if (nwriteoffs + nsize >= m_d->m_maxsize) {
        // If we are at the biggest allowed size or we are currently
        // growing a young file, the oldest header is at BOT.
        m_d->m_oheadoffs = CIRCACHE_FIRSTBLOCK_SIZE;
    }
    return m_d->writefirstblock();
    return true;
}

bool CirCache::rewind(bool& eof)
{
    assert(m_d != 0);

    eof = false;

    // Read oldest header
    m_d->m_itoffs = m_d->m_oheadoffs;
    CCScanHook::status st = m_d->readentryheader(m_d->m_itoffs, m_d->m_ithd);

    switch(st) {
    case CCScanHook::Eof:
        eof = true;
        return false;
    case CCScanHook::Continue:
        return true;
    default:
        return false;
    }
}

bool CirCache::next(bool& eof)
{
    assert(m_d != 0);

    eof = false;

    // Skip to next header, using values stored from previous one
    m_d->m_itoffs += CIRCACHE_HEADER_SIZE + m_d->m_ithd.dicsize + 
        m_d->m_ithd.datasize + m_d->m_ithd.padsize;

    // Looped back ?
    if (m_d->m_itoffs == m_d->m_oheadoffs) {
        eof = true;
        return false;
    }

    // Read. If we hit physical eof, fold.
    CCScanHook::status st = m_d->readentryheader(m_d->m_itoffs, m_d->m_ithd);
    if (st == CCScanHook::Eof) {
        m_d->m_itoffs = CIRCACHE_FIRSTBLOCK_SIZE;
        if (m_d->m_itoffs == m_d->m_oheadoffs) {
            // Then the file is not folded yet (still growing)
            eof = true;
            return false;
        }
        st = m_d->readentryheader(m_d->m_itoffs, m_d->m_ithd);
    }

    if (st == CCScanHook::Continue)
        return true;
    return false;
}

bool CirCache::getcurrentdict(string& dic)
{
    assert(m_d != 0);
    if (!m_d->readDicData(m_d->m_itoffs, m_d->m_ithd, dic, 0))
        return false;
    return true;
}

bool CirCache::getcurrent(string& udi, string& dic, string& data)
{
    assert(m_d != 0);
    if (!m_d->readDicData(m_d->m_itoffs, m_d->m_ithd, dic, &data))
        return false;

    ConfSimple conf(dic, 1);
    conf.get("udi", udi, "");
    return true;
}

void *
allocmem(void 	*cp,	/* The array to grow. may be NULL */
	 int	 sz,	/* Unit size in bytes */
	 int 	*np,    /* Pointer to current allocation number */
	 int	 min,   /* Number to allocate the first time */
	 int	 maxinc) /* Maximum increment */
{
    if (cp == 0) {
        cp = malloc(min * sz);
        *np = cp ? min : 0;
        return cp;
    }

    int inc = (*np > maxinc) ?  : *np;
    if ((cp = realloc(cp, (*np + inc) * sz)) != 0)
        *np += inc;
    return cp;
}

static bool inflateToDynBuf(void* inp, UINT inlen, void **outpp, UINT *outlenp)
{
    z_stream d_stream; /* decompression stream */

    LOGDEB0(("inflateToDynBuf: inlen %u\n", inlen));

    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    d_stream.opaque = (voidpf)0;
    // Compression works well on html files, 4-6 is quite common, Otoh we
    // maybe passed a big, little if at all compressed image or pdf file,
    // So we set the initial allocation at 3 times the input size
    const int imul = 3;
    const int mxinc = 20;
    char *outp = 0;
    int alloc = 0;
    d_stream.next_in  = (Bytef*)inp;
    d_stream.avail_in = inlen;
    d_stream.next_out = 0;
    d_stream.avail_out = 0;

    int err;
    if ((err = inflateInit(&d_stream)) != Z_OK) {
        LOGERR(("Inflate: inflateInit: err %d msg %s\n", err, d_stream.msg));
        free(outp);
        return false;
    }

    for (;;) {
        LOGDEB2(("InflateToDynBuf: avail_in %d total_in %d avail_out %d "
                 "total_out %d\n", d_stream.avail_in, d_stream.total_in,
                 d_stream.avail_out, d_stream.total_out));
        if (d_stream.avail_out == 0) {
            if ((outp = (char*)allocmem(outp, inlen, &alloc, 
                                                 imul, mxinc)) == 0) {
                LOGERR(("Inflate: out of memory, current alloc %d\n", 
                        alloc*inlen));
                inflateEnd(&d_stream);
                return false;
            } else {
                LOGDEB2(("inflateToDynBuf: realloc(%d) ok\n", alloc * inlen));
            }
            d_stream.avail_out = alloc * inlen - d_stream.total_out;
            d_stream.next_out = (Bytef*)(outp + d_stream.total_out);
        }
        err = inflate(&d_stream, Z_NO_FLUSH);
        if (err == Z_STREAM_END) break;
        if (err != Z_OK) {
            LOGERR(("Inflate: error %d msg %s\n", err, d_stream.msg));
            inflateEnd(&d_stream);
            free(outp);
            return false;
        }
    }
    *outlenp = d_stream.total_out;
    *outpp = (Bytef *)outp;

    if ((err = inflateEnd(&d_stream)) != Z_OK) {
        LOGERR(("Inflate: inflateEnd error %d msg %s\n", err, d_stream.msg));
        return false;
    }
    LOGDEB0(("inflateToDynBuf: ok, output size %d\n", d_stream.total_out));
    return true;
}

#else // TEST ->

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <string>
#include <iostream>

#include "circache.h"
#include "fileudi.h"
#include "conftree.h"
#include "readfile.h"
#include "debuglog.h"

using namespace std;

static char *thisprog;

static char usage [] =
" -c <dirname> : create\n"
" -p <dirname> <apath> [apath ...] : put files\n"
" -d <dirname> : dump\n"
" -g [-i instance] [-D] <dirname> <udi>: get\n"
"   -D: also dump data\n"
;
static void
Usage(FILE *fp = stderr)
{
    fprintf(fp, "%s: usage:\n%s", thisprog, usage);
    exit(1);
}

static int     op_flags;
#define OPT_MOINS 0x1
#define OPT_c	  0x2 
#define OPT_p     0x8
#define OPT_g     0x10
#define OPT_d     0x20
#define OPT_i     0x40
#define OPT_D     0x80
int main(int argc, char **argv)
{
  int instance = -1;

  thisprog = argv[0];
  argc--; argv++;

  while (argc > 0 && **argv == '-') {
    (*argv)++;
    if (!(**argv))
      /* Cas du "adb - core" */
      Usage();
    while (**argv)
      switch (*(*argv)++) {
      case 'c':	op_flags |= OPT_c; break;
      case 'p':	op_flags |= OPT_p; break;
      case 'g':	op_flags |= OPT_g; break;
      case 'd':	op_flags |= OPT_d; break;
      case 'D':	op_flags |= OPT_D; break;
      case 'i':	op_flags |= OPT_i; if (argc < 2)  Usage();
	if ((sscanf(*(++argv), "%d", &instance)) != 1) 
	  Usage(); 
	argc--; 
	goto b1;
      default: Usage();	break;
      }
  b1: argc--; argv++;
  }

  DebugLog::getdbl()->setloglevel(DEBDEB1);
  DebugLog::setfilename("stderr");

  if (argc < 1)
    Usage();
  string dir = *argv++;argc--;

  CirCache cc(dir);

  if (op_flags & OPT_c) {
      if (!cc.create(100*1024)) {
          cerr << "Create failed:" << cc.getReason() << endl;
          exit(1);
      }
  } else if (op_flags & OPT_p) {
      if (argc < 1)
          Usage();
      if (!cc.open(CirCache::CC_OPWRITE)) {
          cerr << "Open failed: " << cc.getReason() << endl;
          exit(1);
      }
      while (argc) {
          string fn = *argv++;argc--;
          char dic[1000];
          string data, reason;
          if (!file_to_string(fn, data, &reason)) {
              cerr << "File_to_string: " << reason << endl;
              exit(1);
          }
          string udi;
          make_udi(fn, "", udi);
          sprintf(dic, "#whatever...\nmimetype = text/plain\nudi=%s\n", 
                  udi.c_str());
          string sdic;
          sdic.assign(dic, strlen(dic));
          ConfSimple conf(sdic);
   
          if (!cc.put(udi, &conf, data, 0)) {
              cerr << "Put failed: " << cc.getReason() << endl;
              cerr << "conf: ["; conf.write(cerr); cerr << "]" << endl;
              exit(1);
          }
      }
      cc.open(CirCache::CC_OPREAD);
  } else if (op_flags & OPT_g) {
      if (!cc.open(CirCache::CC_OPREAD)) {
          cerr << "Open failed: " << cc.getReason() << endl;
          exit(1);
      }
      while (argc) {
          string udi = *argv++;argc--;
          string dic, data;
          if (!cc.get(udi, dic, data, instance)) {
              cerr << "Get failed: " << cc.getReason() << endl;
              exit(1);
          }
          cout << "Dict: [" << dic << "]" << endl;
          if (op_flags & OPT_D)
              cout << "Data: [" << data << "]" << endl;
      }
  } else if (op_flags & OPT_d) {
      if (!cc.open(CirCache::CC_OPREAD)) {
          cerr << "Open failed: " << cc.getReason() << endl;
          exit(1);
      }
      cc.dump();
  } else
      Usage();

  exit(0);
}

#endif
