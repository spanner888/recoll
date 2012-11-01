/*
 *   Copyright 2004 J.F.Dockes
 *
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
#include "autoconfig.h"

#include <errno.h>
#include <iostream>
#include <string>
#include <vector>
#include <list>
using namespace std;

#include "cstr.h"
#include "mimehandler.h"
#include "debuglog.h"
#include "rclconfig.h"
#include "smallut.h"

#include "mh_exec.h"
#include "mh_execm.h"
#include "mh_html.h"
#include "mh_mail.h"
#include "mh_mbox.h"
#include "mh_text.h"
#include "mh_unknown.h"
#include "ptmutex.h"

// Performance help: we use a pool of already known and created
// handlers. There can be several instances for a given mime type
// (think email attachment in email message: 2 rfc822 handlers are
// needed simulteanously)
static multimap<string, Dijon::Filter*>  o_handlers;
static list<multimap<string, Dijon::Filter*>::iterator> o_hlru;
typedef list<multimap<string, Dijon::Filter*>::iterator>::iterator hlruit_tp;

static PTMutexInit o_handlers_mutex;

static const unsigned int max_handlers_cache_size = 100;

/* Look for mime handler in pool */
static Dijon::Filter *getMimeHandlerFromCache(const string& key)
{
    PTMutexLocker locker(o_handlers_mutex);
    LOGDEB(("getMimeHandlerFromCache: %s cache size %u\n", 
	    key.c_str(), o_handlers.size()));

    multimap<string, Dijon::Filter *>::iterator it = o_handlers.find(key);
    if (it != o_handlers.end()) {
	Dijon::Filter *h = it->second;
	hlruit_tp it1 = find(o_hlru.begin(), o_hlru.end(), it);
	if (it1 != o_hlru.end()) {
	    o_hlru.erase(it1);
	} else {
	    LOGERR(("getMimeHandlerFromCache: lru position not found\n"));
	}
	o_handlers.erase(it);
	LOGDEB(("getMimeHandlerFromCache: %s found size %u\n", 
		key.c_str(), o_handlers.size()));
	return h;
    }
    LOGDEB(("getMimeHandlerFromCache: %s not found\n", key.c_str()));
    return 0;
}

/* Return mime handler to pool */
void returnMimeHandler(Dijon::Filter *handler)
{
    typedef multimap<string, Dijon::Filter*>::value_type value_type;

    if (handler==0) 
	return;
    handler->clear();

    PTMutexLocker locker(o_handlers_mutex);

    LOGDEB(("returnMimeHandler: returning filter for %s cache size %d\n", 
	    handler->get_mime_type().c_str(), o_handlers.size()));

    // Limit pool size. The pool can grow quite big because there are
    // many filter types, each of which can be used in several copies
    // at the same time either because it occurs several times in a
    // stack (ie mail attachment to mail), or because several threads
    // are processing the same mime type at the same time.
    multimap<string, Dijon::Filter *>::iterator it;
    if (o_handlers.size() >= max_handlers_cache_size) {
	static int once = 1;
	if (once) {
	    once = 0;
	    for (it = o_handlers.begin(); it != o_handlers.end(); it++) {
		LOGERR(("Cache full key: %s\n", it->first.c_str()));
	    }
	    LOGERR(("Cache LRU size: %u\n", o_hlru.size()));
	}
	if (o_hlru.size() > 0) {
	    it = o_hlru.back();
	    o_hlru.pop_back();
	    delete it->second;
	    o_handlers.erase(it);
	}
    }
    it = o_handlers.insert(value_type(handler->get_mime_type(), handler));
    o_hlru.push_front(it);
}

void clearMimeHandlerCache()
{
    LOGDEB(("clearMimeHandlerCache()\n"));
    typedef multimap<string, Dijon::Filter*>::value_type value_type;
    map<string, Dijon::Filter *>::iterator it;
    PTMutexLocker locker(o_handlers_mutex);
    for (it = o_handlers.begin(); it != o_handlers.end(); it++) {
	delete it->second;
    }
    o_handlers.clear();
}

/** For mime types set as "internal" in mimeconf: 
  * create appropriate handler object. */
static Dijon::Filter *mhFactory(RclConfig *config, const string &mime)
{
    LOGDEB2(("mhFactory(%s)\n", mime.c_str()));
    string lmime(mime);
    stringtolower(lmime);
    if (cstr_textplain == lmime) {
	LOGDEB2(("mhFactory(%s): returning MimeHandlerText\n", mime.c_str()));
	return new MimeHandlerText(config, lmime);
    } else if ("text/html" == lmime) {
	LOGDEB2(("mhFactory(%s): returning MimeHandlerHtml\n", mime.c_str()));
	return new MimeHandlerHtml(config, lmime);
    } else if ("text/x-mail" == lmime) {
	LOGDEB2(("mhFactory(%s): returning MimeHandlerMbox\n", mime.c_str()));
	return new MimeHandlerMbox(config, lmime);
    } else if ("message/rfc822" == lmime) {
	LOGDEB2(("mhFactory(%s): returning MimeHandlerMail\n", mime.c_str()));
	return new MimeHandlerMail(config, lmime);
    } else if (lmime.find("text/") == 0) {
        // Try to handle unknown text/xx as text/plain. This
        // only happen if the text/xx was defined as "internal" in
        // mimeconf, not at random. For programs, for example this
        // allows indexing and previewing as text/plain (no filter
        // exec) but still opening with a specific editor.
	LOGDEB2(("mhFactory(%s): returning MimeHandlerText(x)\n",mime.c_str()));
        return new MimeHandlerText(config, lmime); 
    } else {
	// We should not get there. It means that "internal" was set
	// as a handler in mimeconf for a mime type we actually can't
	// handle.
	LOGERR(("mhFactory: mime type [%s] set as internal but unknown\n", 
		lmime.c_str()));
	return new MimeHandlerUnknown(config, lmime);
    }
}

static const string cstr_mh_charset("charset");
/**
 * Create a filter that executes an external program or script
 * A filter def can look like:
 *      someprog -v -t " h i j";charset= xx; mimetype=yy
 * A semi-colon list of attr=value pairs may come after the exec spec.
 * This list is treated by replacing semi-colons with newlines and building
 * a confsimple. This is done quite brutally and we don't support having
 * a ';' inside a quoted string for now. Can't see a use for it.
 */
MimeHandlerExec *mhExecFactory(RclConfig *cfg, const string& mtype, string& hs,
                               bool multiple)
{
    ConfSimple attrs;
    string cmdstr;
    if (!cfg->valueSplitAttributes(hs, cmdstr, attrs)) {
	LOGERR(("mhExecFactory: bad config line for [%s]: [%s]\n", 
		mtype.c_str(), hs.c_str()));
        return 0;
    }

    // Split command name and args, and build exec object
    list<string> cmdtoks;
    stringToStrings(cmdstr, cmdtoks);
    if (cmdtoks.empty()) {
	LOGERR(("mhExecFactory: bad config line for [%s]: [%s]\n", 
		mtype.c_str(), hs.c_str()));
	return 0;
    }
    MimeHandlerExec *h = multiple ? 
        new MimeHandlerExecMultiple(cfg, mtype.c_str()) :
        new MimeHandlerExec(cfg, mtype.c_str());
    list<string>::iterator it = cmdtoks.begin();
    h->params.push_back(cfg->findFilter(*it++));
    h->params.insert(h->params.end(), it, cmdtoks.end());

    // Handle additional attributes. We substitute the semi-colons
    // with newlines and use a ConfSimple
    string value;
    if (attrs.get(cstr_mh_charset, value)) 
        h->cfgFilterOutputCharset = stringtolower((const string&)value);
    if (attrs.get(cstr_dj_keymt, value))
        h->cfgFilterOutputMtype = stringtolower((const string&)value);

#if 0
    string scmd;
    for (it = h->params.begin(); it != h->params.end(); it++) {
	scmd += string("[") + *it + "] ";
    }
    LOGDEB(("mhExecFactory:mt [%s] cfgmt [%s] cfgcs [%s] cmd: [%s]\n",
	    mtype.c_str(), h->cfgFilterOutputMtype.c_str(), h->cfgFilterOutputCharset.c_str(),
	    scmd.c_str()));
#endif

    return h;
}

/* Get handler/filter object for given mime type: */
Dijon::Filter *getMimeHandler(const string &mtype, RclConfig *cfg, 
			      bool filtertypes)
{
    LOGDEB(("getMimeHandler: mtype [%s] filtertypes %d\n", 
	     mtype.c_str(), filtertypes));
    Dijon::Filter *h = 0;

    // Get handler definition for mime type. We do this even if an
    // appropriate handler object may be in the cache (indexed by mime
    // type). This is fast, and necessary to conform to the
    // configuration, (ie: text/html might be filtered out by
    // indexedmimetypes but an html handler could still be in the
    // cache because it was needed by some other interning stack).
    string hs;
    hs = cfg->getMimeHandlerDef(mtype, filtertypes);

    if (!hs.empty()) { // Got a handler definition line

        // Do we already have a handler object in the cache ?
	h = getMimeHandlerFromCache(mtype);
	if (h != 0)
	    goto out;
	LOGDEB2(("getMimeHandler: %s not in cache\n", mtype.c_str()));

	// Not in cache. Break definition into type and name/command
        // string and instanciate handler object
        string::size_type b1 = hs.find_first_of(" \t");
        string handlertype = hs.substr(0, b1);
	string cmdstr;
	if (b1 != string::npos) {
	    cmdstr = hs.substr(b1);
            trimstring(cmdstr);
	}
	if (!stringlowercmp("internal", handlertype)) {
	    // If there is a parameter after "internal" it's the mime
	    // type to use. This is so that we can have bogus mime
	    // types like text/x-purple-html-log (for ie: specific
	    // icon) and still use the html filter on them. This is
	    // partly redundant with the localfields/rclaptg, but
	    // better and the latter will probably go away at some
	    // point in the future.
	    LOGDEB2(("handlertype internal, cmdstr [%s]\n", cmdstr.c_str()));
	    if (!cmdstr.empty()) {
		// Have to redo the cache thing. Maybe we should
		// rather just recurse instead ?
		if ((h = getMimeHandlerFromCache(cmdstr)) == 0)
		    h = mhFactory(cfg, cmdstr);
	    } else {
		h = mhFactory(cfg, mtype);
	    }
	    goto out;
	} else if (!stringlowercmp("dll", handlertype)) {
	} else {
            if (cmdstr.empty()) {
		LOGERR(("getMimeHandler: bad line for %s: %s\n", 
			mtype.c_str(), hs.c_str()));
		goto out;
	    }
            if (!stringlowercmp("exec", handlertype)) {
                h = mhExecFactory(cfg, mtype, cmdstr, false);
		goto out;
            } else if (!stringlowercmp("execm", handlertype)) {
                h = mhExecFactory(cfg, mtype, cmdstr, true);
		goto out;
            } else {
		LOGERR(("getMimeHandler: bad line for %s: %s\n", 
			mtype.c_str(), hs.c_str()));
		goto out;
            }
	}
    }

    // We get here if there was no specific error, but there is no
    // identified mime type, or no handler associated.

    // Finally, unhandled files are either ignored or their name and
    // generic metadata is indexed, depending on configuration
    {bool indexunknown = false;
	cfg->getConfParam("indexallfilenames", &indexunknown);
	if (indexunknown) {
	    if ((h = getMimeHandlerFromCache("application/octet-stream")) == 0)
		h = new MimeHandlerUnknown(cfg, "application/octet-stream");
	    goto out;
	} else {
	    goto out;
	}
    }

out:
    if (h) {
	h->set_property(Dijon::Filter::DEFAULT_CHARSET, cfg->getDefCharset());
    }
    return h;
}

/// Can this mime type be interned (according to config) ?
bool canIntern(const std::string mtype, RclConfig *cfg)
{
    if (mtype.empty())
	return false;
    string hs = cfg->getMimeHandlerDef(mtype);
    if (hs.empty())
	return false;
    return true;
}
