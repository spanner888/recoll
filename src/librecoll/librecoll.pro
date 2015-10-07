#-------------------------------------------------
#
# Project created by QtCreator 2015-10-03T09:04:49
#
#-------------------------------------------------

QT       -= core gui

TARGET = librecoll
TEMPLATE = lib

DEFINES += LIBRECOLL_LIBRARY BUILDING_RECOLL
DEFINES -= UNICODE
DEFINES -= _UNICODE
DEFINES += _MBCS

SOURCES += \
../aspell/rclaspell.cpp \
../bincimapmime/convert.cc \
../bincimapmime/mime-parsefull.cc \
../bincimapmime/mime-parseonlyheader.cc \
../bincimapmime/mime-printbody.cc \
../bincimapmime/mime.cc \
../common/beaglequeuecache.cpp \
../common/cstr.cpp \
../common/rclconfig.cpp \
../common/rclinit.cpp \
../common/syngroups.cpp \
../common/textsplit.cpp \
../common/unacpp.cpp \
../index/beaglequeue.cpp \
../index/bglfetcher.cpp \
../index/checkretryfailed.cpp \
../index/fetcher.cpp \
../index/fsfetcher.cpp \
../index/fsindexer.cpp \
../index/indexer.cpp \
../index/mimetype.cpp \
../index/subtreelist.cpp \
../internfile/extrameta.cpp \
../internfile/htmlparse.cpp \
../internfile/internfile.cpp \
../internfile/mh_exec.cpp \
../internfile/mh_execm.cpp \
../internfile/mh_html.cpp \
../internfile/mh_mail.cpp \
../internfile/mh_mbox.cpp \
../internfile/mh_text.cpp \
../internfile/mimehandler.cpp \
../internfile/myhtmlparse.cpp \
../internfile/txtdcode.cpp \
../internfile/uncomp.cpp \
../query/docseq.cpp \
../query/docseqdb.cpp \
../query/docseqhist.cpp \
../query/dynconf.cpp \
../query/filtseq.cpp \
../query/plaintorich.cpp \
../query/recollq.cpp \
../query/reslistpager.cpp \
../query/sortseq.cpp \
../query/wasaparse.cpp \
../query/wasaparseaux.cpp \
../rcldb/daterange.cpp \
../rcldb/expansiondbs.cpp \
../rcldb/rclabstract.cpp \
../rcldb/rcldb.cpp \
../rcldb/rcldoc.cpp \
../rcldb/rcldups.cpp \
../rcldb/rclquery.cpp \
../rcldb/rclterms.cpp \
../rcldb/searchdata.cpp \
../rcldb/searchdatatox.cpp \
../rcldb/searchdataxml.cpp \
../rcldb/stemdb.cpp \
../rcldb/stoplist.cpp \
../rcldb/synfamily.cpp \
../unac/unac.cpp \
../utils/appformime.cpp \
../utils/base64.cpp \
../utils/circache.cpp \
../utils/conftree.cpp \
../utils/copyfile.cpp \
../utils/cpuconf.cpp \
../utils/debuglog.cpp \
../utils/ecrontab.cpp \
../windows/execmd_w.cpp \
../windows/fnmatch.c \
../utils/fileudi.cpp \
../utils/fstreewalk.cpp \
../utils/idfile.cpp \
../utils/md5.cpp \
../utils/md5ut.cpp \
../utils/mimeparse.cpp \
../utils/pathut.cpp \
../utils/pxattr.cpp \
../utils/rclionice.cpp \
../utils/readfile.cpp \
../utils/smallut.cpp \
../utils/strmatcher.cpp \
../utils/transcode.cpp \
../utils/wipedir.cpp \
../windows/strptime.cpp \
../windows/dirent.c

HEADERS += 

INCLUDEPATH += ../common ../index ../internfile ../query ../unac \
              ../utils ../aspell ../rcldb ../qtgui ../xaposix \
              ../confgui ../bincimapmime ../windows \
              c:/recolldeps/xapian/xapian-core-1.2.8/include

windows{
    contains(QMAKE_CC, gcc){
        # MingW
        QMAKE_CXXFLAGS += -std=c++11 -Wno-unused-parameter
    }
    contains(QMAKE_CC, cl){
        # Visual Studio
    }
  LIBS += c:/recolldeps/xapian/xapian-core-1.2.21/.libs/libxapian-22.dll \
       c:/recolldeps/zlib-1.2.8/zlib1.dll -liconv -lshlwapi -lkernel32
}

unix {
    target.path = /usr/lib
    INSTALLS += target
}
