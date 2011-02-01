#ifndef lint
static char rcsid[] = "@(#$Id: mh_exec.cpp,v 1.14 2008-10-09 09:19:37 dockes Exp $ (C) 2005 J.F.Dockes";
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
#include <stdio.h>

#include <iostream>
#include <sstream>

#include "mh_execm.h"
#include "mh_html.h"
#include "debuglog.h"
#include "cancelcheck.h"
#include "smallut.h"
#include "transcode.h"
#include "md5.h"
#include "rclconfig.h"
#include "mimetype.h"
#include "idfile.h"

#include <sys/types.h>
#include <sys/wait.h>

#ifndef NO_NAMESPACES
using namespace std;
#endif /* NO_NAMESPACES */

bool MimeHandlerExecMultiple::startCmd()
{
    LOGDEB(("MimeHandlerExecMultiple::startCmd\n"));
    if (params.empty()) {
	// Hu ho
	LOGERR(("MHExecMultiple::startCmd: empty params\n"));
	m_reason = "RECFILTERROR BADCONFIG";
	return false;
    }

    // Command name
    string cmd = params.front();
    
    // Build parameter list: delete cmd name
    list<string>::iterator it = params.begin();
    list<string>myparams(++it, params.end());

    // Start filter
    m_cmd.putenv(m_forPreview ? "RECOLL_FILTER_FORPREVIEW=yes" :
		"RECOLL_FILTER_FORPREVIEW=no");
    if (m_cmd.startExec(cmd, myparams, 1, 1) < 0) {
        m_reason = string("RECFILTERROR HELPERNOTFOUND ") + cmd;
        missingHelper = true;
        return false;
    }
    return true;
}

// Note: data is not used if this is the "document:" field: it goes
// directly to m_metaData["content"] to avoid an extra copy
// 
// Messages are made of data elements. Each element is like:
// name: len\ndata
// An empty line signals the end of the message, so the whole thing
// would look like:
// Name1: Len1\nData1Name2: Len2\nData2\n
bool MimeHandlerExecMultiple::readDataElement(string& name, string &data)
{
    string ibuf;

    // Read name and length
    if (m_cmd.getline(ibuf) <= 0) {
        LOGERR(("MHExecMultiple: getline error\n"));
        return false;
    }
    // Empty line (end of message) ?
    if (!ibuf.compare("\n")) {
        LOGDEB(("MHExecMultiple: Got empty line\n"));
        name = "";
        return true;
    }

    // Filters will sometimes abort before entering the real protocol, ie if
    // a module can't be loaded. Check the special filter error first word:
    if (ibuf.find("RECFILTERROR ") == 0) {
        m_reason = ibuf;
        if (ibuf.find("HELPERNOTFOUND") != string::npos)
            missingHelper = true;
        return false;
    }

    // We're expecting something like Name: len\n
    vector<string> tokens;
    stringToTokens(ibuf, tokens);
    if (tokens.size() != 2) {
        LOGERR(("MHExecMultiple: bad line in filter output: [%s]\n",
                ibuf.c_str()));
        return false;
    }
    vector<string>::iterator it = tokens.begin();
    name = *it++;
    string& slen = *it;
    int len;
    if (sscanf(slen.c_str(), "%d", &len) != 1) {
        LOGERR(("MHExecMultiple: bad line in filter output: [%s]\n",
                ibuf.c_str()));
        return false;
    }
    LOGDEB1(("MHExecMultiple: got name [%s] len: %d\n", name.c_str(), len));

    // Hack: check for 'Document:' and read directly the document data
    // to m_metaData["content"] to avoid an extra copy of the bulky
    // piece
    string *datap = &data;
    if (!stringlowercmp("document:", name)) {
        datap = &m_metaData["content"];
    } else {
        datap = &data;
    }

    // Read element data
    datap->erase();
    if (len > 0 && m_cmd.receive(*datap, len) != len) {
        LOGERR(("MHExecMultiple: expected %d bytes of data, got %d\n", 
                len, datap->length()));
        return false;
    }
    return true;
}

bool MimeHandlerExecMultiple::next_document()
{
    LOGDEB(("MimeHandlerExecMultiple::next_document(): [%s]\n", m_fn.c_str()));
    if (m_havedoc == false)
	return false;

    if (missingHelper) {
	LOGDEB(("MHExecMultiple::next_document(): helper known missing\n"));
	return false;
    }

    if (m_cmd.getChildPid() < 0 && !startCmd()) {
        return false;
    }

    // Send request to child process. This maybe the first/only
    // request for a given file, or a continuation request. We send an
    // empty file name in the latter case.
    ostringstream obuf;
    if (m_filefirst) {
        obuf << "FileName: " << m_fn.length() << "\n" << m_fn;
        // m_filefirst is set to true by set_document_file()
        m_filefirst = false;
    } else {
        obuf << "Filename: " << 0 << "\n";
    }
    if (m_ipath.length()) {
        obuf << "Ipath: " << m_ipath.length() << "\n" << m_ipath;
    }
    if (!m_dfltInputCharset.empty()) {
        obuf << "DflInCS: " << m_dfltInputCharset.length() << "\n" 
	     << m_dfltInputCharset;
    }
    obuf << "Mimetype: " << m_mimeType.length() << "\n" << m_mimeType;
    obuf << "\n";
    if (m_cmd.send(obuf.str()) < 0) {
        m_cmd.zapChild();
        LOGERR(("MHExecMultiple: send error\n"));
        return false;
    }

    // Read answer (multiple elements)
    LOGDEB1(("MHExecMultiple: reading answer\n"));
    bool eofnext_received = false;
    bool eofnow_received = false;
    bool fileerror_received = false;
    bool subdocerror_received = false;
    string ipath;
    string mtype;
    string charset;
    for (int loop=0;;loop++) {
        string name, data;
        if (!readDataElement(name, data)) {
            m_cmd.zapChild();
            return false;
        }
        if (name.empty())
            break;
        if (!stringlowercmp("eofnext:", name)) {
            LOGDEB(("MHExecMultiple: got EOFNEXT\n"));
            eofnext_received = true;
        } else if (!stringlowercmp("eofnow:", name)) {
            LOGDEB(("MHExecMultiple: got EOFNOW\n"));
            eofnow_received = true;
        } else if (!stringlowercmp("fileerror:", name)) {
            LOGDEB(("MHExecMultiple: got FILEERROR\n"));
	    fileerror_received = true;
        } else if (!stringlowercmp("subdocerror:", name)) {
            LOGDEB(("MHExecMultiple: got SUBDOCERROR\n"));
	    subdocerror_received = true;
        } else if (!stringlowercmp("ipath:", name)) {
            ipath = data;
            LOGDEB(("MHExecMultiple: got ipath [%s]\n", data.c_str()));
        } else if (!stringlowercmp("charset:", name)) {
            charset = data;
            LOGDEB(("MHExecMultiple: got ipath [%s]\n", data.c_str()));
        } else if (!stringlowercmp("mimetype:", name)) {
            mtype = data;
            LOGDEB(("MHExecMultiple: got mimetype [%s]\n", data.c_str()));
        }
        if (loop == 10) {
            // ?? 
            LOGERR(("MHExecMultiple: filter sent too many parameters\n"));
            return false;
        }
    }

    if (eofnow_received || fileerror_received) {
        // No more docs
        m_havedoc = false;
        return false;
    }
    if (subdocerror_received) {
	return false;
    }

    // It used to be that eof could be signalled just by an empty document, but
    // this was wrong. Empty documents can be found ie in zip files and should 
    // not be interpreted as eof.
    if (m_metaData["content"].length() == 0) {
        LOGDEB0(("MHExecMultiple: got empty document inside [%s]: [%s]\n", 
                m_fn.c_str(), ipath.c_str()));
    }

    // If this has an ipath, it is an internal doc from a
    // multi-document file. In this case, either the filter supplies the 
    // mimetype, or the ipath MUST be a filename-like string which we can use
    // to compute a mime type
    if (!ipath.empty()) {
        m_metaData["ipath"] = ipath;
        if (mtype.empty()) {
            mtype = mimetype(ipath, 0, RclConfig::getMainConfig(), false);
            if (mtype.empty()) {
                // mimetype() won't call idFile when there is no file. Do it
                mtype = idFileMem(m_metaData["content"]);
                if (mtype.empty()) {
                    // Note this happens for example for directory zip members
                    // We could recognize them by the end /, but wouldn't know
                    // what to do with them anyway.
                    LOGINFO(("MHExecMultiple: cant guess mime type\n"));
                    mtype = "application/octet-stream";
                }
            }
        }
        m_metaData["mimetype"] = mtype;
        string md5, xmd5;
        MD5String(m_metaData["content"], md5);
        m_metaData["md5"] = MD5HexPrint(md5, xmd5);
    } else {
        m_metaData["mimetype"] = mtype.empty() ? "text/html" : mtype;
        m_metaData.erase("ipath");
        string md5, xmd5, reason;
        if (MD5File(m_fn, md5, &reason)) {
            m_metaData["md5"] = MD5HexPrint(md5, xmd5);
        } else {
            LOGERR(("MimeHandlerExecM: cant compute md5 for [%s]: %s\n",
                    m_fn.c_str(), reason.c_str()));
        }
    }

    // Charset. For many document types it doesn't matter. For text
    // and html it does. We supply a default from the
    // configuration. We should do the text transcoding to utf-8 here
    // like exec::finaldetails does.
    if (charset.empty()) {
	charset = cfgFilterOutputCharset.empty() ? "utf-8" : 
	    cfgFilterOutputCharset;
	if (!stringlowercmp("default", charset)) {
	    charset = m_dfltInputCharset;
	}
    }
    m_metaData["charset"] = charset;
    
    if (eofnext_received)
        m_havedoc = false;

    return true;
}
