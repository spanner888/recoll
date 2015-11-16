#!/usr/bin/env python
# Copyright (C) 2014 J.F.Dockes
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the
# Free Software Foundation, Inc.,
# 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

# Recoll PDF extractor, with support for attachments

from __future__ import print_function

import os
import sys
import re
import rclexecm
import subprocess
import tempfile
import atexit
import signal

tmpdir = None

def finalcleanup():
    if tmpdir:
        vacuumdir(tmpdir)
        os.rmdir(tmpdir)

def signal_handler(signal, frame):
    sys.exit(1)

atexit.register(finalcleanup)

try:
    signal.signal(signal.SIGHUP, signal_handler)
except:
    pass
try:
    signal.signal(signal.SIGINT, signal_handler)
except:
    pass
try:
    signal.signal(signal.SIGQUIT, signal_handler)
except:
    pass
try:
    signal.signal(signal.SIGTERM, signal_handler)
except:
    pass

def vacuumdir(dir):
    if dir:
        for fn in os.listdir(dir):
            path = os.path.join(dir, fn)
            if os.path.isfile(path):
                os.unlink(path)
    return True

class PDFExtractor:
    def __init__(self, em):
        self.currentindex = 0
        self.pdftotext = ""
        self.pdftk = ""
        self.em = em
        self.attextractdone = False
        self.attachlist = []
        
    # Extract all attachments if any into temporary directory
    def extractAttach(self):
        if self.attextractdone:
            return True
        self.attextractdone = True

        global tmpdir
        if not tmpdir or not self.pdftk:
            # no big deal
            return True

        try:
            vacuumdir(tmpdir)
            subprocess.check_call([self.pdftk, self.filename, "unpack_files",
                                   "output", tmpdir])
            self.attachlist = sorted(os.listdir(tmpdir))
            return True
        except Exception as e:
            self.em.rclog("extractAttach: failed: %s" % e)
            # Return true anyway, pdf attachments are no big deal
            return True

    def extractone(self, ipath):
        #self.em.rclog("extractone: [%s]" % ipath)
        if not self.attextractdone:
            if not self.extractAttach():
                return (False, "", "", rclexecm.RclExecM.eofnow)
        path = os.path.join(tmpdir, ipath)
        if os.path.isfile(path):
            f = open(path)
            docdata = f.read();
            f.close()
        if self.currentindex == len(self.attachlist) - 1:
            eof = rclexecm.RclExecM.eofnext
        else:
            eof = rclexecm.RclExecM.noteof
        return (True, docdata, ipath, eof)

    # pdftotext (used to?) badly escape text inside the header
    # fields. We do it here. This is not an html parser, and depends a
    # lot on the actual format output by pdftotext.
    def _fixhtml(self, input):
        #print input
        inheader = False
        inbody = False
        didcs = False
        output = b''
        cont = b''
        for line in input.split(b'\n'):
            line = cont + line
            cont = b''
            if re.search(b'</head>', line):
                inheader = False
            if re.search(b'</pre>', line):
                inbody = False
            if inheader:
                if not didcs:
                    output += b'<meta http-equiv="Content-Type"' + \
                              b'content="text/html; charset=UTF-8">\n'
                    didcs = True

                m = re.search(rb'(.*<title>)(.*)(<\/title>.*)', line)
                if not m:
                    m = re.search(rb'(.*content=")(.*)(".*/>.*)', line)
                if m:
                    line = m.group(1) + self.em.htmlescape(m.group(2)) + \
                           m.group(3)

                # Recoll treats "Subject" as a "title" element
                # (based on emails). The PDF "Subject" metadata
                # field is more like an HTML "description"
                line = re.sub(b'name="Subject"', b'name="Description"', line, 1)

            elif inbody:
                # Remove end-of-line hyphenation. It's not clear that
                # we should do this as pdftotext without the -layout
                # option does it ?
                #if re.search(r'[-]$', line):
                    #m = re.search(r'(.*)[ \t]([^ \t]+)$', line)
                    #if m:
                        #line = m.group(1)
                        #cont = m.group(2).rstrip('-')
                line = self.em.htmlescape(line)
                
            if re.search(b'<head>', line):
                inheader = True
            if re.search(b'<pre>', line):
                inbody = True

            output += line + b'\n'

        return output
            
    def _selfdoc(self):
        self.em.setmimetype('text/html')

        if self.attextractdone and len(self.attachlist) == 0:
            eof = rclexecm.RclExecM.eofnext
        else:
            eof = rclexecm.RclExecM.noteof
            
        data = subprocess.check_output([self.pdftotext, "-htmlmeta", "-enc",
                                        "UTF-8", "-eol", "unix", "-q",
                                        self.filename, "-"])
        data = self._fixhtml(data)
        #self.em.rclog("%s" % data)
        return (True, data, "", eof)
    
    ###### File type handler api, used by rclexecm ---------->
    def openfile(self, params):
        self.filename = params["filename:"]
        #self.em.rclog("openfile: [%s]" % self.filename)
        self.currentindex = -1
        self.attextractdone = False

        if not self.pdftotext:
            self.pdftotext = rclexecm.which("pdftotext")
            if not self.pdftotext:
                self.pdftotext = rclexecm.which("poppler/pdftotext")
            if not self.pdftotext:
                print("RECFILTERROR HELPERNOTFOUND pdftotext")
                sys.exit(1);

        if not self.pdftk:
            self.pdftk = rclexecm.which("pdftk")

        if self.pdftk:
            global tmpdir
            if tmpdir:
                if not vacuumdir(tmpdir):
                    self.em.rclog("openfile: vacuumdir %s failed" % tmpdir)
                    return False
            else:
                tmpdir = tempfile.mkdtemp(prefix='rclmpdf')

            preview = os.environ.get("RECOLL_FILTER_FORPREVIEW", "no")
            if preview != "yes":
                # When indexing, extract attachments at once. This
                # will be needed anyway and it allows generating an
                # eofnext error instead of waiting for actual eof,
                # which avoids a bug in recollindex up to 1.20
                self.extractAttach()
        else:
            self.attextractdone = True
        return True

    def getipath(self, params):
        ipath = params["ipath:"]
        ok, data, ipath, eof = self.extractone(ipath)
        return (ok, data, ipath, eof)
        
    def getnext(self, params):
        # self.em.rclog("getnext: current %d" % self.currentindex)
        if self.currentindex == -1:
            self.currentindex = 0
            return self._selfdoc()
        else:
            self.em.setmimetype('')

            if not self.attextractdone:
                if not self.extractAttach():
                    return (False, "", "", rclexecm.RclExecM.eofnow)

            if self.currentindex >= len(self.attachlist):
                return (False, "", "", rclexecm.RclExecM.eofnow)
            try:
                ok, data, ipath, eof = \
                    self.extractone(self.attachlist[self.currentindex])
                self.currentindex += 1

                #self.em.rclog("getnext: returning ok for [%s]" % ipath)
                return (ok, data, ipath, eof)
            except:
                return (False, "", "", rclexecm.RclExecM.eofnow)


# Main program: create protocol handler and extractor and run them
proto = rclexecm.RclExecM()
extract = PDFExtractor(proto)
rclexecm.main(proto, extract)
