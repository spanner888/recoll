# Note this is generated by configure on Linux (see recoll.pro.in).
# recoll.pro is under version control anyway and used on Windows

TEMPLATE        = app
LANGUAGE        = C++
TARGET          = recoll

QT += webkit

DEFINES += BUILDING_RECOLL
DEFINES -= UNICODE
DEFINES -= _UNICODE
DEFINES += _MBCS
DEFINES += PSAPI_VERSION=1

QT += xml
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets webkitwidgets printsupport

HEADERS += \
        advsearch_w.h \
        advshist.h \
        confgui/confgui.h \
        confgui/confguiindex.h \
        crontool.h \
        widgets/editdialog.h \
        firstidx.h \
        fragbuts.h \
        idxsched.h \
        widgets/listdialog.h \
        widgets/qxtconfirmationmessage.h \
        preview_w.h \
        preview_load.h \
        preview_plaintorich.h \
        ptrans_w.h \
        rclhelp.h \
        rclmain_w.h \
        reslist.h \
        restable.h \
        rtitool.h \
        searchclause_w.h \
        snippets_w.h \
        specialindex.h \
        spell_w.h \
        ssearch_w.h \
        systray.h \
        uiprefs_w.h \
        viewaction_w.h \
        webcache.h

SOURCES += \
        advsearch_w.cpp \
        advshist.cpp \
        confgui/confgui.cpp \
        confgui/confguiindex.cpp \
        crontool.cpp \
        fragbuts.cpp \
        guiutils.cpp \
        main.cpp \
        multisave.cpp \
        preview_w.cpp \
        preview_load.cpp \
        preview_plaintorich.cpp \
        ptrans_w.cpp \
        rclhelp.cpp \
        rclmain_w.cpp \
        rclm_idx.cpp \
        rclm_preview.cpp \
        rclm_saveload.cpp \
        rclm_view.cpp \
        rclm_wins.cpp \
        rclzg.cpp \
        respopup.cpp \
        reslist.cpp \
        restable.cpp \
        rtitool.cpp \
        searchclause_w.cpp \
        snippets_w.cpp \
        spell_w.cpp \
        ssearch_w.cpp \
        systray.cpp \
        uiprefs_w.cpp \
        viewaction_w.cpp \
        webcache.cpp \
        widgets/qxtconfirmationmessage.cpp \
        xmltosd.cpp

FORMS   = \
        advsearch.ui \
        crontool.ui \
        widgets/editdialog.ui \
        firstidx.ui \
        idxsched.ui \
        widgets/listdialog.ui \
        ptrans.ui \
        rclmain.ui \
        restable.ui \
        rtitool.ui \
        specialindex.ui \
        spell.ui \
        snippets.ui \
        ssearchb.ui \
        uiprefs.ui \
        viewaction.ui \
        webcache.ui

RESOURCES = recoll.qrc

INCLUDEPATH += ../common ../index ../internfile ../query ../unac \
              ../utils ../aspell ../rcldb ../qtgui ../xaposix \
              confgui widgets
windows {
    RC_FILE = recoll.rc
    contains(QMAKE_CC, gcc){
        # MingW
        QMAKE_CXXFLAGS += -std=c++11 -Wno-unused-parameter
    }
    contains(QMAKE_CC, cl){
        # Visual Studio
    }
    LIBS += C:/recoll/src/windows/build-librecoll-Desktop_Qt_5_5_0_MinGW_32bit-Release/release/librecoll.dll
}

TRANSLATIONS = \
	     i18n/recoll_cs.ts \
	     i18n/recoll_da.ts \
	     i18n/recoll_de.ts \
	     i18n/recoll_el.ts \
	     i18n/recoll_es.ts \
             i18n/recoll_fr.ts \
             i18n/recoll_it.ts \
             i18n/recoll_lt.ts \
             i18n/recoll_ru.ts \
             i18n/recoll_tr.ts \
             i18n/recoll_uk.ts \
             i18n/recoll_xx.ts \
             i18n/recoll_zh_CN.ts \
             i18n/recoll_zh.ts \
