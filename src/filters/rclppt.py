#!/usr/bin/env python

import rclexecm
import rclexec1
import re
import sys
import os

class PPTProcessData:
    def __init__(self, em):
        self.em = em
        self.out = ""
        self.gotdata = 0

    def takeLine(self, line):
        if not self.gotdata:
            self.out += '''<html><head>''' + \
                        '''<meta http-equiv="Content-Type" ''' + \
                        '''content="text/html;charset=UTF-8">''' + \
                        '''</head><body><pre>'''
            self.gotdata = True
        self.out += self.em.htmlescape(line) + "<br>\n"

    def wrapData(self):
        return self.out + '''</pre></body></html>'''

class PPTFilter:
    def __init__(self, em):
        self.em = em
        self.ntry = 0

    def reset(self):
        self.ntry = 0
        pass
            
    def getCmd(self, fn):
        if self.ntry:
            return ([], None)
        self.ntry = 1
        cmd = rclexecm.which("ppt-dump.py")
        if cmd:
            # ppt-dump.py often exits 1 with valid data. Ignore exit value
            return (["python", cmd, "--no-struct-output", "--dump-text"],
                    PPTProcessData(self.em), rclexec1.Executor.opt_ignxval)
        else:
            return ([], None)

if __name__ == '__main__':
    if not rclexecm.which("ppt-dump.py"):
        print("RECFILTERROR HELPERNOTFOUND ppt-dump.py")
        sys.exit(1)
    proto = rclexecm.RclExecM()
    filter = PPTFilter(proto)
    extract = rclexec1.Executor(proto, filter)
    rclexecm.main(proto, extract)
