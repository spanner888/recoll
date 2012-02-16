from distutils.core import setup, Extension
import os
import sys

sysname = os.uname()[0]
if sysname == 'Linux':
    libs = ['rcl', 'xapian']
else:
    libs = ['rcl', 'xapian', 'iconv']

if 'RECOLL_DATADIR' in os.environ:
    datadirs = [os.environ['RECOLL_DATADIR']]
else:
    datadirs = []
datadirs = datadirs + ['/usr/share/recoll', '/usr/local/share/recoll', '/']
for datadir in datadirs:
    if os.path.exists(datadir):
        break
if datadir == '/':
    print 'share/recoll directory not found: you need to install Recoll first'
    sys.exit(1)

top = os.path.join('..', '..')

# Test for PIC library
localdefs = os.path.join(top, 'mk', 'localdefs')
try:
    lines = open(localdefs, 'r').readlines()
except:
    print 'You need to build recoll first. Use configure --enable-pic'
    sys.exit(1)
picok = False
for line in lines:
    if line.find('PICFLAGS') == 0:
        picok = True
        break
if not picok:
    print 'You need to rebuild recoll with PIC enabled. Use configure --enable-pic and make clean'
    sys.exit(1)

                               
module1 = Extension('recoll',
                    define_macros = [('MAJOR_VERSION', '1'),
                                     ('MINOR_VERSION', '0'),
                                     ('UNAC_VERSION', '"1.0.7"'),
                                     ('RECOLL_DATADIR', '"' + datadir + '"')
                                     ],
                    include_dirs = ['/usr/local/include',
                                    os.path.join(top, 'utils'), 
                                    os.path.join(top, 'common'), 
                                    os.path.join(top, 'rcldb'), 
                                    os.path.join(top, 'query'), 
                                    os.path.join(top, 'unac')
                                    ],
                    libraries = libs,
                    library_dirs = [os.path.join(top, 'lib'), '/usr/local/lib'],
                    sources = ['pyrecoll.cpp',
                               ])

setup (name = 'Recoll',
       version = '1.0',
       description = 'Query/Augment a Recoll full text index',
       author = 'J.F. Dockes',
       author_email = 'jfd@recoll.org',
       long_description = '''
''',
       ext_modules = [module1])
