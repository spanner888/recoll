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
#ifndef TEST_RCLCONFIG
#include "autoconfig.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <langinfo.h>
#include <limits.h>

#include <algorithm>
#include <list>
using std::list;

#include <sys/types.h>
#include <sys/stat.h>
#ifdef __FreeBSD__
#include <osreldate.h>
#endif

#include <iostream>
#include <cstdlib>
#include <cstring>
using namespace std;

#include "cstr.h"
#include "pathut.h"
#include "rclconfig.h"
#include "conftree.h"
#include "debuglog.h"
#include "smallut.h"
#include "textsplit.h"
#include "readfile.h"
#include "fstreewalk.h"

#ifndef RCL_INDEX_STRIPCHARS
// We default to a case- and diacritics-less index for now
bool o_index_stripchars = true;
#endif

bool ParamStale::needrecompute()
{
    LOGDEB2(("ParamStale:: needrecompute. parent gen %d mine %d\n", 
	     parent->m_keydirgen, savedkeydirgen));
    if (parent->m_keydirgen != savedkeydirgen) {
	LOGDEB2(("ParamState:: needrecompute. conffile %p\n", conffile));

        savedkeydirgen = parent->m_keydirgen;
        string newvalue;
        if (!conffile)
            return false;
        conffile->get(paramname, newvalue, parent->m_keydir);
        if (newvalue.compare(savedvalue)) {
            savedvalue = newvalue;
	    LOGDEB2(("ParamState:: needrecompute. return true newvalue [%s]\n",
		     newvalue.c_str()));
            return true;
        }
    }
    return false;
}

void ParamStale::init(RclConfig *rconf, ConfNull *cnf, const string& nm)
{
    parent = rconf;
    conffile = cnf;
    paramname = nm;
    savedkeydirgen = -1;
}

void RclConfig::zeroMe() {
    m_ok = false; 
    m_keydirgen = 0;
    m_conf = 0; 
    mimemap = 0; 
    mimeconf = 0; 
    mimeview = 0; 
    m_fields = 0;
    m_stopsuffixes = 0;
    m_maxsufflen = 0;
    m_stpsuffstate.init(this, 0, "recoll_noindex");
    m_skpnstate.init(this, 0, "skippedNames");
    m_rmtstate.init(this, 0, "indexedmimetypes");
}

bool RclConfig::isDefaultConfig() const
{
    string defaultconf = path_cat(path_canon(path_home()), ".recoll/");
    string specifiedconf = path_canon(m_confdir);
    path_catslash(specifiedconf);
    return !defaultconf.compare(specifiedconf);
}

string RclConfig::o_localecharset; 

RclConfig::RclConfig(const string *argcnf)
{
    zeroMe();
    // Compute our data dir name, typically /usr/local/share/recoll
    const char *cdatadir = getenv("RECOLL_DATADIR");
    if (cdatadir == 0) {
	// If not in environment, use the compiled-in constant. 
	m_datadir = RECOLL_DATADIR;
    } else {
	m_datadir = cdatadir;
    }

    // We only do the automatic configuration creation thing for the default
    // config dir, not if it was specified through -c or RECOLL_CONFDIR
    bool autoconfdir = false;

    // Command line config name overrides environment
    if (argcnf && !argcnf->empty()) {
	m_confdir = path_absolute(*argcnf);
	if (m_confdir.empty()) {
	    m_reason = 
		string("Cant turn [") + *argcnf + "] into absolute path";
	    return;
	}
    } else {
	const char *cp = getenv("RECOLL_CONFDIR");
	if (cp) {
	    m_confdir = cp;
	} else {
	    autoconfdir = true;
	    m_confdir = path_cat(path_home(), ".recoll/");
	}
    }

    // Note: autoconfdir and isDefaultConfig() are normally the same. We just 
    // want to avoid the imperfect test in isDefaultConfig() if we actually know
    // this is the default conf
    if (!autoconfdir && !isDefaultConfig()) {
	if (access(m_confdir.c_str(), 0) < 0) {
	    m_reason = "Explicitly specified configuration "
		"directory must exist"
		" (won't be automatically created). Use mkdir first";
	    return;
	}
    }

    if (access(m_confdir.c_str(), 0) < 0) {
	if (!initUserConfig()) 
	    return;
    }

    // This can't change once computed inside a process. It would be
    // nicer to move this to a static class initializer to avoid
    // possible threading issues but this doesn't work (tried) as
    // things would not be ready. In practise we make sure that this
    // is called from the main thread at once, by constructing a config
    // from recollinit
    if (o_localecharset.empty()) {
	const char *cp;
	cp = nl_langinfo(CODESET);
	// We don't keep US-ASCII. It's better to use a superset
	// Ie: me have a C locale and some french file names, and I
	// can't imagine a version of iconv that couldn't translate
	// from iso8859?
	// The 646 thing is for solaris. 
	if (cp && *cp && strcmp(cp, "US-ASCII") 
#ifdef sun
	    && strcmp(cp, "646")
#endif
	    ) {
	    o_localecharset = string(cp);
	} else {
	    // Use cp1252 instead of iso-8859-1, it's a superset.
	    o_localecharset = string(cstr_cp1252);
	}
	LOGDEB1(("RclConfig::getDefCharset: localecharset [%s]\n",
		 o_localecharset.c_str()));
    }

    m_cdirs.push_back(m_confdir);
    m_cdirs.push_back(path_cat(m_datadir, "examples"));
    string cnferrloc = m_confdir + " or " + path_cat(m_datadir, "examples");

    // Read and process "recoll.conf"
    if (!updateMainConfig())
	return;
    // Other files
    mimemap = new ConfStack<ConfTree>("mimemap", m_cdirs, true);
    if (mimemap == 0 || !mimemap->ok()) {
	m_reason = string("No or bad mimemap file in: ") + cnferrloc;
	return;
    }
    mimeconf = new ConfStack<ConfSimple>("mimeconf", m_cdirs, true);
    if (mimeconf == 0 || !mimeconf->ok()) {
	m_reason = string("No/bad mimeconf in: ") + cnferrloc;
	return;
    }
    mimeview = new ConfStack<ConfSimple>("mimeview", m_cdirs, false);
    if (mimeview == 0)
	mimeview = new ConfStack<ConfSimple>("mimeview", m_cdirs, true);
    if (mimeview == 0 || !mimeview->ok()) {
	m_reason = string("No/bad mimeview in: ") + cnferrloc;
	return;
    }
    if (!readFieldsConfig(cnferrloc))
	return;

    m_ok = true;
    setKeyDir(cstr_null);

    m_stpsuffstate.init(this, mimemap, "recoll_noindex");
    m_skpnstate.init(this, m_conf, "skippedNames");
    m_rmtstate.init(this, m_conf, "indexedmimetypes");
    return;
}

bool RclConfig::updateMainConfig()
{
    ConfStack<ConfTree> *newconf = 
	new ConfStack<ConfTree>("recoll.conf", m_cdirs, true);
    if (newconf == 0 || !newconf->ok()) {
	if (m_conf)
	    return false;
	string where;
	stringsToString(m_cdirs, where);
	m_reason = string("No/bad main configuration file in: ") + where;
	m_ok = false;
        m_skpnstate.init(this, 0, "skippedNames");
        m_rmtstate.init(this, 0, "indexedmimetypes");
	return false;
    }
    delete m_conf;
    m_conf = newconf;
    m_skpnstate.init(this, m_conf, "skippedNames");
    m_rmtstate.init(this, m_conf, "indexedmimetypes");


    setKeyDir(cstr_null);
    bool nocjk = false;
    if (getConfParam("nocjk", &nocjk) && nocjk == true) {
	TextSplit::cjkProcessing(false);
    } else {
	int ngramlen;
	if (getConfParam("cjkngramlen", &ngramlen)) {
	    TextSplit::cjkProcessing(true, (unsigned int)ngramlen);
	} else {
	    TextSplit::cjkProcessing(true);
	}
    }

    bool nonum = false;
    if (getConfParam("nonumbers", &nonum) && nonum == true) {
	TextSplit::noNumbers();
    }

    bool fnmpathname = true;
    if (getConfParam("skippedPathsFnmPathname", &fnmpathname)
	&& fnmpathname == false) {
	FsTreeWalker::setNoFnmPathname();
    }

#ifndef RCL_INDEX_STRIPCHARS
    static int m_index_stripchars_init = 0;
    if (!m_index_stripchars_init) {
	getConfParam("indexStripChars", &o_index_stripchars);
	m_index_stripchars_init = 1;
    }
#endif

    return true;
}

ConfNull *RclConfig::cloneMainConfig()
{
    ConfNull *conf = new ConfStack<ConfTree>("recoll.conf", m_cdirs, false);
    if (conf == 0 || !conf->ok()) {
	m_reason = string("Can't read config");
	return 0;
    }
    return conf;
}

// Remember what directory we're under (for further conf->get()s), and 
// prefetch a few common values.
void RclConfig::setKeyDir(const string &dir) 
{
    if (!dir.compare(m_keydir))
        return;

    m_keydirgen++;
    m_keydir = dir;
    if (m_conf == 0)
	return;

    if (!m_conf->get("defaultcharset", m_defcharset, m_keydir))
	m_defcharset.erase();
}

bool RclConfig::getConfParam(const string &name, int *ivp) const
{
    string value;
    if (!getConfParam(name, value))
	return false;
    errno = 0;
    long lval = strtol(value.c_str(), 0, 0);
    if (lval == 0 && errno)
	return 0;
    if (ivp)
	*ivp = int(lval);
    return true;
}

bool RclConfig::getConfParam(const string &name, bool *bvp) const
{
    if (!bvp) 
	return false;

    *bvp = false;
    string s;
    if (!getConfParam(name, s))
	return false;
    *bvp = stringToBool(s);
    return true;
}

bool RclConfig::getConfParam(const string &name, vector<string> *svvp) const
{
    if (!svvp) 
	return false;
    svvp->clear();
    string s;
    if (!getConfParam(name, s))
	return false;
    return stringToStrings(s, *svvp);
}

bool RclConfig::getConfParam(const string &name, vector<int> *vip) const
{
    if (!vip) 
	return false;
    vip->clear();
    vector<string> vs;
    if (!getConfParam(name, &vs))
	return false;
    vip->reserve(vs.size());
    for (unsigned int i = 0; i < vs.size(); i++) {
	char *ep;
	vip->push_back(strtol(vs[i].c_str(), &ep, 0));
	if (ep == vs[i].c_str()) {
	    LOGDEB(("RclConfig::getConfParam: bad int value in [%s]\n",
		    name.c_str()));
	    return false;
	}
    }
    return true;
}

pair<int,int> RclConfig::getThrConf(ThrStage who) const
{
    vector<int> vq;
    vector<int> vt;
    if (!getConfParam("thrQSizes", &vq) || !getConfParam("thrTCounts", &vt)) {
	return pair<int,int>(-1,-1);
    }
    return pair<int,int>(vq[who], vt[who]);
}

vector<string> RclConfig::getTopdirs() const
{
    vector<string> tdl;
    if (!getConfParam("topdirs", &tdl)) {
	LOGERR(("RclConfig::getTopdirs: no top directories in config or "
		"bad list format\n"));
	return tdl;
    }

    for (vector<string>::iterator it = tdl.begin(); it != tdl.end(); it++) {
	*it = path_tildexpand(*it);
	*it = path_canon(*it);
    }
    return tdl;
}

// Get charset to be used for transcoding to utf-8 if unspecified by doc
// For document contents:
//  If defcharset was set (from the config or a previous call, this
//   is done in setKeydir), use it.
//  Else, try to guess it from the locale
//  Use cp1252 (as a superset of iso8859-1) as ultimate default
//
// For filenames, same thing except that we do not use the config file value
// (only the locale).
const string& RclConfig::getDefCharset(bool filename) const
{
    if (filename) {
	return o_localecharset;
    } else {
	return m_defcharset.empty() ? o_localecharset : m_defcharset;
    }
}

bool RclConfig::addLocalFields(map<string, string> *tgt) const
{
    LOGDEB0(("RclConfig::addLocalFields: keydir [%s]\n", m_keydir.c_str()));
    string sfields;
    if (tgt == 0 || ! getConfParam("localfields", sfields))
        return false;
    // Substitute ':' with '\n' inside the string. There is no way to escape ':'
    for (string::size_type i = 0; i < sfields.size(); i++)
        if (sfields[i] == ':')
            sfields[i] = '\n';
    // Parse the result with a confsimple and add the results to the metadata
    ConfSimple conf(sfields, 1, true);
    vector<string> nmlst = conf.getNames(cstr_null);
    for (vector<string>::const_iterator it = nmlst.begin();
         it != nmlst.end(); it++) {
        conf.get(*it, (*tgt)[*it]);
        LOGDEB(("RclConfig::addLocalFields: [%s] => [%s]\n",
		(*it).c_str(), (*tgt)[*it].c_str()));
    }
    return true;
}

// Get all known document mime values. We get them from the mimeconf
// 'index' submap.
// It's quite possible that there are other mime types in the index
// (defined in mimemap and not mimeconf, or output by "file -i"). We
// just ignore them, because there may be myriads, and their contents
// are not indexed. 
//
// This unfortunately means that searches by file names and mime type
// filtering don't work well together.
vector<string> RclConfig::getAllMimeTypes() const
{
    vector<string> lst;
    if (mimeconf == 0)
	return lst;
    lst = mimeconf->getNames("index");
    return lst;
}

// Things for suffix comparison. We define a string class and string 
// comparison with suffix-only sensitivity
class SfString {
public:
    SfString(const string& s) : m_str(s) {}
    bool operator==(const SfString& s2) {
	string::const_reverse_iterator r1 = m_str.rbegin(), re1 = m_str.rend(),
	    r2 = s2.m_str.rbegin(), re2 = s2.m_str.rend();
	while (r1 != re1 && r2 != re2) {
	    if (*r1 != *r2) {
		return 0;
	    }
	    ++r1; ++r2;
	}
	return 1;
    }
    string m_str;
};

class SuffCmp {
public:
    int operator()(const SfString& s1, const SfString& s2) {
	//cout << "Comparing " << s1.m_str << " and " << s2.m_str << endl;
	string::const_reverse_iterator 
	    r1 = s1.m_str.rbegin(), re1 = s1.m_str.rend(),
	    r2 = s2.m_str.rbegin(), re2 = s2.m_str.rend();
	while (r1 != re1 && r2 != re2) {
	    if (*r1 != *r2) {
		return *r1 < *r2 ? 1 : 0;
	    }
	    ++r1; ++r2;
	}
	return 0;
    }
};
typedef multiset<SfString, SuffCmp> SuffixStore;

#define STOPSUFFIXES ((SuffixStore *)m_stopsuffixes)

bool RclConfig::inStopSuffixes(const string& fni)
{
    LOGDEB2(("RclConfig::inStopSuffixes(%s)\n", fni.c_str()));
    // Beware: needrecompute() needs to be called always. 2nd test stays back.
    if (m_stpsuffstate.needrecompute() || m_stopsuffixes == 0) {
	// Need to initialize the suffixes
        delete STOPSUFFIXES;
	if ((m_stopsuffixes = new SuffixStore) == 0) {
	    LOGERR(("RclConfig::inStopSuffixes: out of memory\n"));
	    return false;
	}
	list<string> stoplist;
        stringToStrings(m_stpsuffstate.savedvalue, stoplist);
	for (list<string>::const_iterator it = stoplist.begin(); 
	     it != stoplist.end(); it++) {
	    STOPSUFFIXES->insert(SfString(stringtolower(*it)));
	    if (m_maxsufflen < it->length())
		m_maxsufflen = it->length();
	}
    }

    // Only need a tail as long as the longest suffix.
    int pos = MAX(0, int(fni.length() - m_maxsufflen));
    string fn(fni, pos);

    stringtolower(fn);
    SuffixStore::const_iterator it = STOPSUFFIXES->find(fn);
    if (it != STOPSUFFIXES->end()) {
	LOGDEB2(("RclConfig::inStopSuffixes: Found (%s) [%s]\n", 
		fni.c_str(), (*it).m_str.c_str()));
	return true;
    } else {
	LOGDEB2(("RclConfig::inStopSuffixes: not found [%s]\n", fni.c_str()));
	return false;
    }
}

string RclConfig::getMimeTypeFromSuffix(const string& suff) const
{
    string mtype;
    mimemap->get(suff, mtype, m_keydir);
    return mtype;
}

string RclConfig::getSuffixFromMimeType(const string &mt) const
{
    string suffix;
    vector<string>sfs = mimemap->getNames(cstr_null);
    string mt1;
    for (vector<string>::const_iterator it = sfs.begin(); 
	 it != sfs.end(); it++) {
	if (mimemap->get(*it, mt1, cstr_null))
	    if (!stringicmp(mt, mt1))
		return *it;
    }
    return cstr_null;
}

/** Get list of file categories from mimeconf */
bool RclConfig::getMimeCategories(vector<string>& cats) const
{
    if (!mimeconf)
	return false;
    cats = mimeconf->getNames("categories");
    return true;
}

bool RclConfig::isMimeCategory(string& cat) const
{
    vector<string>cats;
    getMimeCategories(cats);
    for (vector<string>::iterator it = cats.begin(); it != cats.end(); it++) {
	if (!stringicmp(*it,cat))
	    return true;
    }
    return false;
}

/** Get list of mime types for category from mimeconf */
bool RclConfig::getMimeCatTypes(const string& cat, vector<string>& tps) const
{
    tps.clear();
    if (!mimeconf)
	return false;
    string slist;
    if (!mimeconf->get(cat, slist, "categories"))
	return false;

    stringToStrings(slist, tps);
    return true;
}

string RclConfig::getMimeHandlerDef(const string &mtype, bool filtertypes)
{
    string hs;
    if (filtertypes && m_rmtstate.needrecompute()) {
        m_restrictMTypes.clear();
        stringToStrings(stringtolower((const string&)m_rmtstate.savedvalue), 
                        m_restrictMTypes);
    }
    if (filtertypes && !m_restrictMTypes.empty()) {
	string mt = mtype;
        stringtolower(mt);
	if (m_restrictMTypes.find(mt) == m_restrictMTypes.end())
	    return hs;
    }
    if (!mimeconf->get(mtype, hs, "index")) {
	LOGDEB1(("getMimeHandler: no handler for '%s'\n", mtype.c_str()));
    }
    return hs;
}

bool RclConfig::getGuiFilterNames(vector<string>& cats) const
{
    if (!mimeconf)
	return false;
    cats = mimeconf->getNamesShallow("guifilters");
    return true;
}

bool RclConfig::getGuiFilter(const string& catfiltername, string& frag) const
{
    frag.clear();
    if (!mimeconf)
	return false;
    if (!mimeconf->get(catfiltername, frag, "guifilters"))
	return false;
    return true;
}

bool RclConfig::valueSplitAttributes(const string& whole, string& value, 
				     ConfSimple& attrs)
{
    /* There is currently no way to escape a semi-colon */
    string::size_type semicol0 = whole.find_first_of(";");
    value = whole.substr(0, semicol0);
    trimstring(value);
    string attrstr;
    if (semicol0 != string::npos && semicol0 < whole.size() - 1) {
        attrstr = whole.substr(semicol0+1);
    }

    // Handle additional attributes. We substitute the semi-colons
    // with newlines and use a ConfSimple
    if (!attrstr.empty()) {
        for (string::size_type i = 0; i < attrstr.size(); i++)
            if (attrstr[i] == ';')
                attrstr[i] = '\n';
        attrs = ConfSimple(attrstr);
    }
    return true;
}


bool RclConfig::getMissingHelperDesc(string& out) const
{
    string fmiss = path_cat(getConfDir(), "missing");
    out.clear();
    if (!file_to_string(fmiss, out))
	return false;
    return true;
}

void RclConfig::storeMissingHelperDesc(const string &s)
{
    string fmiss = path_cat(getConfDir(), "missing");
    FILE *fp = fopen(fmiss.c_str(), "w");
    if (fp) {
	if (s.size() > 0 && fwrite(s.c_str(), s.size(), 1, fp) != 1) {
            LOGERR(("storeMissingHelperDesc: fwrite failed\n"));
        }
	fclose(fp);
    }
}

// Read definitions for field prefixes, aliases, and hierarchy and arrange 
// things for speed (theses are used a lot during indexing)
bool RclConfig::readFieldsConfig(const string& cnferrloc)
{
    LOGDEB2(("RclConfig::readFieldsConfig\n"));
    m_fields = new ConfStack<ConfSimple>("fields", m_cdirs, true);
    if (m_fields == 0 || !m_fields->ok()) {
	m_reason = string("No/bad fields file in: ") + cnferrloc;
	return false;
    }

    // Build a direct map avoiding all indirections for field to
    // prefix translation
    // Add direct prefixes from the [prefixes] section
    vector<string>tps = m_fields->getNames("prefixes");
    for (vector<string>::const_iterator it = tps.begin(); 
	 it != tps.end(); it++) {
	string val;
	m_fields->get(*it, val, "prefixes");
	ConfSimple attrs;
	FieldTraits ft;
	if (!valueSplitAttributes(val, ft.pfx, attrs)) {
	    LOGERR(("readFieldsConfig: bad config line for [%s]: [%s]\n", 
		    it->c_str(), val.c_str()));
	    return 0;
	}
	string tval;
	if (attrs.get("wdfinc", tval))
	    ft.wdfinc = atoi(tval.c_str());
	if (attrs.get("boost", tval))
	    ft.boost = atof(tval.c_str());
	m_fldtotraits[stringtolower(*it)] = ft;
	LOGDEB2(("readFieldsConfig: [%s] -> [%s] %d %.1f\n", 
		it->c_str(), ft.pfx.c_str(), ft.wdfinc, ft.boost));
    }

    // Add prefixes for aliases  an build alias-to-canonic map while we're at it
    // Having the aliases in the prefix map avoids an additional indirection
    // at index time.
    tps = m_fields->getNames("aliases");
    for (vector<string>::const_iterator it = tps.begin(); it != tps.end();it++) {
	string canonic = stringtolower(*it); // canonic name
	FieldTraits ft;
	map<string, FieldTraits>::const_iterator pit = 
	    m_fldtotraits.find(canonic);
	if (pit != m_fldtotraits.end()) {
	    ft = pit->second;
	}
	string aliases;
	m_fields->get(canonic, aliases, "aliases");
	vector<string> l;
	stringToStrings(aliases, l);
	for (vector<string>::const_iterator ait = l.begin();
	     ait != l.end(); ait++) {
	    if (pit != m_fldtotraits.end())
		m_fldtotraits[stringtolower(*ait)] = ft;
	    m_aliastocanon[stringtolower(*ait)] = canonic;
	}
    }

#if 0
    for (map<string, FieldTraits>::const_iterator it = m_fldtotraits.begin();
	 it != m_fldtotraits.end(); it++) {
	LOGDEB(("readFieldsConfig: [%s] -> [%s] %d %.1f\n", 
		it->c_str(), it->second.pfx.c_str(), it->second.wdfinc, 
		it->second.boost));
    }
#endif

    vector<string> sl = m_fields->getNames("stored");
    if (!sl.empty()) {
	for (vector<string>::const_iterator it = sl.begin(); 
	     it != sl.end(); it++) {
	    string fld = fieldCanon(stringtolower(*it));
	    m_storedFields.insert(fld);
	}
    }

    // Extended file attribute to field translations
    vector<string>xattrs = m_fields->getNames("xattrtofields");
    for (vector<string>::const_iterator it = xattrs.begin(); 
	 it != xattrs.end(); it++) {
	string val;
	m_fields->get(*it, val, "xattrtofields");
	m_xattrtofld[*it] = val;
    }

    return true;
}

// Return specifics for field name:
bool RclConfig::getFieldTraits(const string& _fld, const FieldTraits **ftpp)
    const
{
    string fld = fieldCanon(_fld);
    map<string, FieldTraits>::const_iterator pit = m_fldtotraits.find(fld);
    if (pit != m_fldtotraits.end()) {
	*ftpp = &pit->second;
	LOGDEB1(("RclConfig::getFieldTraits: [%s]->[%s]\n", 
		 _fld.c_str(), pit->second.pfx.c_str()));
	return true;
    } else {
	LOGDEB1(("RclConfig::readFieldsConfig: no prefix for field [%s]\n",
		 fld.c_str()));
	*ftpp = 0;
	return false;
    }
}

set<string> RclConfig::getIndexedFields() const
{
    set<string> flds;
    if (m_fields == 0)
	return flds;

    vector<string> sl = m_fields->getNames("prefixes");
    flds.insert(sl.begin(), sl.end());
    return flds;
}

string RclConfig::fieldCanon(const string& f) const
{
    string fld = stringtolower(f);
    map<string, string>::const_iterator it = m_aliastocanon.find(fld);
    if (it != m_aliastocanon.end()) {
	LOGDEB1(("RclConfig::fieldCanon: [%s] -> [%s]\n", 
		 f.c_str(), it->second.c_str()));
	return it->second;
    }
    LOGDEB1(("RclConfig::fieldCanon: [%s] -> [%s]\n", f.c_str(), fld.c_str()));
    return fld;
}

vector<string> RclConfig::getFieldSectNames(const string &sk, const char* patrn)
    const
{
    if (m_fields == 0)
        return vector<string>();
    return m_fields->getNames(sk, patrn);
}

bool RclConfig::getFieldConfParam(const string &name, const string &sk, 
                                  string &value) const
{
    if (m_fields == 0)
        return false;
    return m_fields->get(name, value, sk);
}

string RclConfig::getMimeViewerAllEx() const
{
    string hs;
    if (mimeview == 0)
	return hs;
    mimeview->get("xallexcepts", hs, "");
    return hs;
}

bool RclConfig::setMimeViewerAllEx(const string& allex)
{
    if (mimeview == 0)
        return false;
    if (!mimeview->set("xallexcepts", allex, "")) {
	m_reason = string("RclConfig:: cant set value. Readonly?");
	return false;
    }

    return true;
}

string RclConfig::getMimeViewerDef(const string &mtype, const string& apptag,
				   bool useall) const
{
    LOGDEB2(("RclConfig::getMimeViewerDef: mtype [%s] apptag [%s]\n",
	     mtype.c_str(), apptag.c_str()));
    string hs;
    if (mimeview == 0)
	return hs;

    if (useall) {
	// Check for exception
	string excepts = getMimeViewerAllEx();
	vector<string> vex;
	stringToTokens(excepts, vex);
	bool isexcept = false;
	for (vector<string>::iterator it = vex.begin();
	     it != vex.end(); it++) {
	    vector<string> mita;
	    stringToTokens(*it, mita, "|");
	    if ((mita.size() == 1 && apptag.empty() && mita[0] == mtype) ||
		(mita.size() == 2 && mita[1] == apptag && mita[0] == mtype)) {
		// Exception to x-all
		isexcept = true;
		break;
	    }
	}

	if (isexcept == false) {
	    mimeview->get("application/x-all", hs, "view");
	    return hs;
	}
	// Fallthrough to normal case.
    }

    if (apptag.empty() || !mimeview->get(mtype + string("|") + apptag,
                                         hs, "view"))
        mimeview->get(mtype, hs, "view");
    return hs;
}

bool RclConfig::getMimeViewerDefs(vector<pair<string, string> >& defs) const
{
    if (mimeview == 0)
	return false;
    vector<string>tps = mimeview->getNames("view");
    for (vector<string>::const_iterator it = tps.begin(); 
	 it != tps.end();it++) {
	defs.push_back(pair<string, string>(*it, getMimeViewerDef(*it, "", 0)));
    }
    return true;
}

bool RclConfig::setMimeViewerDef(const string& mt, const string& def)
{
    if (mimeview == 0)
        return false;
    if (!mimeview->set(mt, def, "view")) {
	m_reason = string("RclConfig:: cant set value. Readonly?");
	return false;
    }
    return true;
}

bool RclConfig::mimeViewerNeedsUncomp(const string &mimetype) const
{
    string s;
    vector<string> v;
    if (mimeview != 0 && mimeview->get("nouncompforviewmts", s, "") && 
        stringToStrings(s, v) && 
        find_if(v.begin(), v.end(), StringIcmpPred(mimetype)) != v.end()) 
        return false;
    return true;
}

string RclConfig::getMimeIconPath(const string &mtype, const string &apptag)
    const
{
    string iconname;
    if (!apptag.empty())
	mimeconf->get(mtype + string("|") + apptag, iconname, "icons");
    if (iconname.empty())
        mimeconf->get(mtype, iconname, "icons");
    if (iconname.empty())
	iconname = "document";

    string iconpath;
#if defined (__FreeBSD__) && __FreeBSD_version < 500000
    // gcc 2.95 dies if we call getConfParam here ??
    if (m_conf) m_conf->get(string("iconsdir"), iconpath, m_keydir);
#else
    getConfParam("iconsdir", iconpath);
#endif

    if (iconpath.empty()) {
	iconpath = path_cat(m_datadir, "images");
    } else {
	iconpath = path_tildexpand(iconpath);
    }
    return path_cat(iconpath, iconname) + ".png";
}

string RclConfig::getDbDir() const
{
    string dbdir;
    if (!getConfParam("dbdir", dbdir)) {
	LOGERR(("RclConfig::getDbDir: no db directory in configuration\n"));
    } else {
	dbdir = path_tildexpand(dbdir);
	// If not an absolute path, compute relative to config dir
	if (dbdir.at(0) != '/') {
	    LOGDEB1(("Dbdir not abs, catting with confdir\n"));
	    dbdir = path_cat(getConfDir(), dbdir);
	}
    }
    LOGDEB1(("RclConfig::getDbDir: dbdir: [%s]\n", dbdir.c_str()));
    return path_canon(dbdir);
}

bool RclConfig::sourceChanged() const
{
    if (m_conf && m_conf->sourceChanged())
	return true;
    if (mimemap && mimemap->sourceChanged())
	return true;
    if (mimeconf && mimeconf->sourceChanged())
	return true;
    if (mimeview && mimeview->sourceChanged())
	return true;
    if (m_fields && m_fields->sourceChanged())
	return true;
    return false;
}

string RclConfig::getStopfile() const
{
    return path_cat(getConfDir(), "stoplist.txt");
}
string RclConfig::getPidfile() const
{
    return path_cat(getConfDir(), "index.pid");
}

// The index status file is fast changing, so it's possible to put it outside
// of the config directory (for ssds, not sure this is really useful).
string RclConfig::getIdxStatusFile() const
{
    string path;
    if (!getConfParam("idxstatusfile", path)) {
	return path_cat(getConfDir(), "idxstatus.txt");
    } else {
	path = path_tildexpand(path);
	// If not an absolute path, compute relative to config dir
	if (path.at(0) != '/') {
	    path = path_cat(getConfDir(), path);
	}
	return path_canon(path);
    }
}

string RclConfig::getWebQueueDir() const
{
    string webqueuedir;
    if (!getConfParam("webqueuedir", webqueuedir))
	webqueuedir = "~/.recollweb/ToIndex/";
    webqueuedir = path_tildexpand(webqueuedir);
    return webqueuedir;
}

vector<string>& RclConfig::getSkippedNames()
{
    if (m_skpnstate.needrecompute()) {
	stringToStrings(m_skpnstate.savedvalue, m_skpnlist);
    }
    return m_skpnlist;
}

vector<string> RclConfig::getSkippedPaths() const
{
    vector<string> skpl;
    getConfParam("skippedPaths", &skpl);

    // Always add the dbdir and confdir to the skipped paths. This is 
    // especially important for the rt monitor which will go into a loop if we
    // don't do this.
    skpl.push_back(getDbDir());
    skpl.push_back(getConfDir());
    // And the web queue dir
    skpl.push_back(getWebQueueDir());
    for (vector<string>::iterator it = skpl.begin(); it != skpl.end(); it++) {
	*it = path_tildexpand(*it);
	*it = path_canon(*it);
    }
    sort(skpl.begin(), skpl.end());
    vector<string>::iterator uit = unique(skpl.begin(), skpl.end());
    skpl.resize(uit - skpl.begin());
    return skpl;
}

vector<string> RclConfig::getDaemSkippedPaths() const
{
    vector<string> dskpl;
    getConfParam("daemSkippedPaths", &dskpl);

    for (vector<string>::iterator it = dskpl.begin(); it != dskpl.end(); it++) {
	*it = path_tildexpand(*it);
	*it = path_canon(*it);
    }

    vector<string> skpl1 = getSkippedPaths();
    vector<string> skpl;
    if (dskpl.empty()) {
	skpl = skpl1;
    } else {
	sort(dskpl.begin(), dskpl.end());
	merge(dskpl.begin(), dskpl.end(), skpl1.begin(), skpl1.end(), 
	      skpl.begin());
	vector<string>::iterator uit = unique(skpl.begin(), skpl.end());
	skpl.resize(uit - skpl.begin());
    }
    return skpl;
}


// Look up an executable filter.  We look in $RECOLL_FILTERSDIR,
// filtersdir in config file, then let the system use the PATH
string RclConfig::findFilter(const string &icmd) const
{
    // If the path is absolute, this is it
    if (icmd[0] == '/')
	return icmd;

    string cmd;
    const char *cp;

    // Filters dir from environment ?
    if ((cp = getenv("RECOLL_FILTERSDIR"))) {
	cmd = path_cat(cp, icmd);
	if (access(cmd.c_str(), X_OK) == 0)
	    return cmd;
    } 
    // Filters dir as configuration parameter?
    if (getConfParam(string("filtersdir"), cmd)) {
	cmd = path_cat(cmd, icmd);
	if (access(cmd.c_str(), X_OK) == 0)
	    return cmd;
    } 

    // Filters dir as datadir subdir. Actually the standard case, but
    // this is normally the same value found in config file (previous step)
    cmd = path_cat(m_datadir, "filters");
    cmd = path_cat(cmd, icmd);
    if (access(cmd.c_str(), X_OK) == 0)
	return cmd;

    // Last resort for historical reasons: check in personal config
    // directory
    cmd = path_cat(getConfDir(), icmd);
    if (access(cmd.c_str(), X_OK) == 0)
	return cmd;

    // Let the shell try to find it...
    return icmd;
}

/** 
 * Return decompression command line for given mime type
 */
bool RclConfig::getUncompressor(const string &mtype, vector<string>& cmd) const
{
    string hs;

    mimeconf->get(mtype, hs, cstr_null);
    if (hs.empty())
	return false;
    vector<string> tokens;
    stringToStrings(hs, tokens);
    if (tokens.empty()) {
	LOGERR(("getUncompressor: empty spec for mtype %s\n", mtype.c_str()));
	return false;
    }
    vector<string>::iterator it = tokens.begin();
    if (tokens.size() < 2)
	return false;
    if (stringlowercmp("uncompress", *it++)) 
	return false;
    cmd.clear();
    cmd.push_back(findFilter(*it++));
    cmd.insert(cmd.end(), it, tokens.end());
    return true;
}

static const char blurb0[] = 
"# The system-wide configuration files for recoll are located in:\n"
"#   %s\n"
"# The default configuration files are commented, you should take a look\n"
"# at them for an explanation of what can be set (you could also take a look\n"
"# at the manual instead).\n"
"# Values set in this file will override the system-wide values for the file\n"
"# with the same name in the central directory. The syntax for setting\n"
"# values is identical.\n"
    ;

// Use uni2ascii -a K to generate these from the utf-8 strings
// Swedish and Danish. 
static const char swedish_ex[] = "unac_except_trans = \303\244\303\244 \303\204\303\244 \303\266\303\266 \303\226\303\266 \303\274\303\274 \303\234\303\274 \303\237ss \305\223oe \305\222oe \303\246ae \303\206ae \357\254\201fi \357\254\202fl \303\245\303\245 \303\205\303\245";
// German:
static const char german_ex[] = "unac_except_trans = \303\244\303\244 \303\204\303\244 \303\266\303\266 \303\226\303\266 \303\274\303\274 \303\234\303\274 \303\237ss \305\223oe \305\222oe \303\246ae \303\206ae \357\254\201fi \357\254\202fl";

// Create initial user config by creating commented empty files
static const char *configfiles[] = {"recoll.conf", "mimemap", "mimeconf", 
				    "mimeview"};
static int ncffiles = sizeof(configfiles) / sizeof(char *);
bool RclConfig::initUserConfig()
{
    // Explanatory text
    const int bs = sizeof(blurb0)+PATH_MAX+1;
    char blurb[bs];
    string exdir = path_cat(m_datadir, "examples");
    snprintf(blurb, bs, blurb0, exdir.c_str());

    // Use protective 700 mode to create the top configuration
    // directory: documents can be reconstructed from index data.
    if (access(m_confdir.c_str(), 0) < 0 && 
	mkdir(m_confdir.c_str(), 0700) < 0) {
	m_reason += string("mkdir(") + m_confdir + ") failed: " + 
	    strerror(errno);
	return false;
    }
    string lang = localelang();
    for (int i = 0; i < ncffiles; i++) {
	string dst = path_cat(m_confdir, string(configfiles[i])); 
	if (access(dst.c_str(), 0) < 0) {
	    FILE *fp = fopen(dst.c_str(), "w");
	    if (fp) {
		fprintf(fp, "%s\n", blurb);
		if (!strcmp(configfiles[i], "recoll.conf")) {
		    // Add improved unac_except_trans for some languages
		    if (lang == "se" || lang == "dk" || lang == "no" || 
			lang == "fi") {
			fprintf(fp, "%s\n", swedish_ex);
		    } else if (lang == "de") {
			fprintf(fp, "%s\n", german_ex);
		    }
		}
		fclose(fp);
	    } else {
		m_reason += string("fopen ") + dst + ": " + strerror(errno);
		return false;
	    }
	}
    }
    return true;
}

void RclConfig::freeAll() 
{
    delete m_conf;
    delete mimemap;
    delete mimeconf; 
    delete mimeview; 
    delete m_fields;
    delete STOPSUFFIXES;
    // just in case
    zeroMe();
}

void RclConfig::initFrom(const RclConfig& r)
{
    zeroMe();
    if (!(m_ok = r.m_ok))
	return;
    m_reason = r.m_reason;
    m_confdir = r.m_confdir;
    m_datadir = r.m_datadir;
    m_keydir = r.m_keydir;
    m_cdirs = r.m_cdirs;
    if (r.m_conf)
	m_conf = new ConfStack<ConfTree>(*(r.m_conf));
    if (r.mimemap)
	mimemap = new ConfStack<ConfTree>(*(r.mimemap));
    if (r.mimeconf)
	mimeconf = new ConfStack<ConfSimple>(*(r.mimeconf));
    if (r.mimeview)
	mimeview = new ConfStack<ConfSimple>(*(r.mimeview));
    if (r.m_fields)
	m_fields = new ConfStack<ConfSimple>(*(r.m_fields));
    m_fldtotraits = r.m_fldtotraits;
    m_aliastocanon = r.m_aliastocanon;
    m_storedFields = r.m_storedFields;
    m_xattrtofld = r.m_xattrtofld;
    if (r.m_stopsuffixes)
	m_stopsuffixes = new SuffixStore(*((SuffixStore*)r.m_stopsuffixes));
    m_maxsufflen = r.m_maxsufflen;
    m_defcharset = r.m_defcharset;

    m_stpsuffstate.init(this, mimemap, r.m_stpsuffstate.paramname);
    m_skpnstate.init(this, m_conf, r.m_skpnstate.paramname);
    m_rmtstate.init(this, m_conf, r.m_rmtstate.paramname);
}

#else // -> Test

#include <stdio.h>
#include <signal.h>

#include <iostream>
#include <vector>
#include <string>

using namespace std;

#include "debuglog.h"
#include "rclinit.h"
#include "rclconfig.h"
#include "cstr.h"

static char *thisprog;

static char usage [] = "\n"
"-c: check a few things in the configuration files\n"
"[-s subkey] -q param : query parameter value\n"
"-f : print some field data\n"
"  : default: print parameters\n"

;
static void
Usage(void)
{
    fprintf(stderr, "%s: usage: %s\n", thisprog, usage);
    exit(1);
}

static int     op_flags;
#define OPT_MOINS 0x1
#define OPT_s	  0x2 
#define OPT_q	  0x4 
#define OPT_c     0x8
#define OPT_f     0x10

int main(int argc, char **argv)
{
    string pname, skey;
    
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
	    case 'f':	op_flags |= OPT_f; break;
	    case 's':	op_flags |= OPT_s; if (argc < 2)  Usage();
		skey = *(++argv);
		argc--; 
		goto b1;
	    case 'q':	op_flags |= OPT_q; if (argc < 2)  Usage();
		pname = *(++argv);
		argc--; 
		goto b1;
	    default: Usage();	break;
	    }
    b1: argc--; argv++;
    }

    if (argc != 0)
	Usage();

    string reason;
    RclConfig *config = recollinit(0, 0, reason);
    if (config == 0 || !config->ok()) {
	cerr << "Configuration problem: " << reason << endl;
	exit(1);
    }
    if (op_flags & OPT_s)
	config->setKeyDir(skey);
    if (op_flags & OPT_q) {
	string value;
	if (!config->getConfParam(pname, value)) {
	    fprintf(stderr, "getConfParam failed for [%s]\n", pname.c_str());
	    exit(1);
	}
	printf("[%s] -> [%s]\n", pname.c_str(), value.c_str());
    } else if (op_flags & OPT_f) {
	set<string> stored = config->getStoredFields();
	set<string> indexed = config->getIndexedFields();
	cout << "Stored fields: ";
        for (set<string>::const_iterator it = stored.begin(); 
             it != stored.end(); it++) {
            cout << "[" << *it << "] ";
        }
	cout << endl;	
	cout << "Indexed fields: ";
        for (set<string>::const_iterator it = indexed.begin(); 
             it != indexed.end(); it++) {
	    const FieldTraits *ftp;
	    config->getFieldTraits(*it, &ftp);
	    if (ftp)
		cout << "[" << *it << "]" << " -> [" << ftp->pfx << "] ";
	    else 
		cout << "[" << *it << "]" << " -> [" << "(none)" << "] ";

        }
	cout << endl;	
    } else if (op_flags & OPT_c) {
	// Checking the configuration consistency
	
	// Find and display category names
        vector<string> catnames;
        config->getMimeCategories(catnames);
        cout << "Categories: ";
        for (vector<string>::const_iterator it = catnames.begin(); 
             it != catnames.end(); it++) {
            cout << *it << " ";
        }
        cout << endl;

	// Compute union of all types from each category. Check that there
	// are no duplicates while we are at it.
        set<string> allmtsfromcats;
        for (vector<string>::const_iterator it = catnames.begin(); 
             it != catnames.end(); it++) {
            vector<string> cts;
            config->getMimeCatTypes(*it, cts);
            for (vector<string>::const_iterator it1 = cts.begin(); 
                 it1 != cts.end(); it1++) {
                // Already in map -> duplicate
                if (allmtsfromcats.find(*it1) != allmtsfromcats.end()) {
                    cout << "Duplicate: [" << *it1 << "]" << endl;
                }
                allmtsfromcats.insert(*it1);
            }
        }

	// Retrieve complete list of mime types 
        vector<string> mtypes = config->getAllMimeTypes();

	// And check that each mime type is found in exactly one category
        for (vector<string>::const_iterator it = mtypes.begin();
             it != mtypes.end(); it++) {
            if (allmtsfromcats.find(*it) == allmtsfromcats.end()) {
                cout << "Not found in catgs: [" << *it << "]" << endl;
            }
        }

	// List mime types not in mimeview
        for (vector<string>::const_iterator it = mtypes.begin();
             it != mtypes.end(); it++) {
	    if (config->getMimeViewerDef(*it, "", false).empty()) {
		cout << "No viewer: [" << *it << "]" << endl;
	    }
        }

	// Check that each mime type has an indexer
        for (vector<string>::const_iterator it = mtypes.begin();
             it != mtypes.end(); it++) {
	    if (config->getMimeHandlerDef(*it, false).empty()) {
		cout << "No filter: [" << *it << "]" << endl;
	    }
        }

	// Check that each mime type has a defined icon
        for (vector<string>::const_iterator it = mtypes.begin();
             it != mtypes.end(); it++) {
	    if (config->getMimeIconPath(*it, "") == "document") {
		cout << "No or generic icon: [" << *it << "]" << endl;
	    }
        }

    } else {
        config->setKeyDir(cstr_null);
	vector<string> names = config->getConfNames();
	for (vector<string>::iterator it = names.begin(); 
	     it != names.end();it++) {
	    string value;
	    config->getConfParam(*it, value);
	    cout << *it << " -> [" << value << "]" << endl;
	}
    }
    exit(0);
}

#endif // TEST_RCLCONFIG
