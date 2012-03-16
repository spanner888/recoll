/* Copyright (C) 2005 J.F.Dockes
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <regex.h>

#include <utility>
#ifndef NO_NAMESPACES
using std::pair;
#endif /* NO_NAMESPACES */

#include <qapplication.h>
#include <qmessagebox.h>
#include <qfiledialog.h>
#include <qshortcut.h>
#include <qtabwidget.h>
#include <qtimer.h>
#include <qstatusbar.h>
#include <qwindowdefs.h>
#include <qcheckbox.h>
#include <qfontdialog.h>
#include <qspinbox.h>
#include <qcombobox.h>
#include <qtextbrowser.h>
#include <qlineedit.h>
#include <qaction.h>
#include <qpushbutton.h>
#include <qimage.h>
#include <qapplication.h>
#include <qcursor.h>
#include <qevent.h>
#include <QFileSystemWatcher>
#include <QThread>
#include <QProgressDialog>

#include "recoll.h"
#include "debuglog.h"
#include "mimehandler.h"
#include "pathut.h"
#include "smallut.h"
#include "advsearch_w.h"
#include "sortseq.h"
#include "uiprefs_w.h"
#include "guiutils.h"
#include "reslist.h"
#include "transcode.h"
#include "refcntr.h"
#include "ssearch_w.h"
#include "execmd.h"
#include "internfile.h"
#include "docseqdb.h"
#include "docseqhist.h"
#include "confguiindex.h"
#include "restable.h"
#include "listdialog.h"
#include "firstidx.h"
#include "idxsched.h"
#include "crontool.h"
#include "rtitool.h"
#include "indexer.h"

using namespace confgui;

#include "rclmain_w.h"
#include "rclhelp.h"
#include "moc_rclmain_w.cpp"

extern "C" int XFlush(void *);
QString g_stringAllStem, g_stringNoStem;
static const QKeySequence quitKeySeq("Ctrl+q");

void RclMain::init()
{
    // This is just to get the common catg strings into the message file
    static const char* catg_strings[] = {
            QT_TR_NOOP("All"), QT_TR_NOOP("media"),  QT_TR_NOOP("message"),
            QT_TR_NOOP("other"),  QT_TR_NOOP("presentation"),
            QT_TR_NOOP("spreadsheet"),  QT_TR_NOOP("text"), 
	    QT_TR_NOOP("sorted"), QT_TR_NOOP("filtered")
    };
    DocSequence::set_translations((const char *)tr("sorted").toUtf8(), 
				(const char *)tr("filtered").toUtf8());

    periodictimer = new QTimer(this);

    // idxstatus file. Make sure it exists before trying to watch it
    // (case where we're started on an older index, or if the status
    // file was deleted since indexing
    ::close(::open(theconfig->getIdxStatusFile().c_str(), O_CREAT, 0600));
    m_watcher.addPath(QString::fromLocal8Bit(
			  theconfig->getIdxStatusFile().c_str()));
    // At least some versions of qt4 don't display the status bar if
    // it's not created here.
    (void)statusBar();

    (void)new HelpClient(this);
    HelpClient::installMap((const char *)this->objectName().toUtf8(), "RCL.SEARCH.SIMPLE");

    // Set the focus to the search terms entry:
    sSearch->queryText->setFocus();

    // Stemming language menu
    g_stringNoStem = tr("(no stemming)");
    g_stringAllStem = tr("(all languages)");
    m_idNoStem = preferencesMenu->addAction(g_stringNoStem);
    m_idNoStem->setCheckable(true);
    m_stemLangToId[g_stringNoStem] = m_idNoStem;
    m_idAllStem = preferencesMenu->addAction(g_stringAllStem);
    m_idAllStem->setCheckable(true);
    m_stemLangToId[g_stringAllStem] = m_idAllStem;

    // Can't get the stemming languages from the db at this stage as
    // db not open yet (the case where it does not even exist makes
    // things complicated). So get the languages from the config
    // instead
    string slangs;
    list<string> langs;
    if (theconfig->getConfParam("indexstemminglanguages", slangs)) {
	stringToStrings(slangs, langs);
    } else {
	QMessageBox::warning(0, "Recoll", 
			     tr("error retrieving stemming languages"));
    }
    QAction *curid = prefs.queryStemLang == "ALL" ? m_idAllStem : m_idNoStem;
    QAction *id; 
    for (list<string>::const_iterator it = langs.begin(); 
	 it != langs.end(); it++) {
	QString qlang = QString::fromAscii(it->c_str(), it->length());
	id = preferencesMenu->addAction(qlang);
	id->setCheckable(true);
	m_stemLangToId[qlang] = id;
	if (prefs.queryStemLang == qlang) {
	    curid = id;
	}
    }
    curid->setChecked(true);

    // Toolbar+combobox version of the category selector
    QComboBox *catgCMB = 0;
    if (prefs.catgToolBar) {
        QToolBar *catgToolBar = new QToolBar(this);
	catgCMB = new QComboBox(catgToolBar);
	catgCMB->setEditable(FALSE);
	catgCMB->addItem(tr("All"));
        catgToolBar->setObjectName(QString::fromUtf8("catgToolBar"));
	catgCMB->setToolTip(tr("Document category filter"));
        catgToolBar->addWidget(catgCMB);
        this->addToolBar(Qt::TopToolBarArea, catgToolBar);
    }

    // Document categories buttons
    QHBoxLayout *bgrphbox = new QHBoxLayout(catgBGRP);
    QButtonGroup *bgrp  = new QButtonGroup(catgBGRP);
    bgrphbox->addWidget(allRDB);
    int bgrpid = 0;
    bgrp->addButton(allRDB, bgrpid++);
    connect(bgrp, SIGNAL(buttonClicked(int)), this, SLOT(catgFilter(int)));
    allRDB->setChecked(true);
    list<string> cats;
    theconfig->getGuiFilterNames(cats);
    // Text for button 0 is not used. Next statement just avoids unused
    // variable compiler warning for catg_strings
    m_catgbutvec.push_back(catg_strings[0]);
    for (list<string>::const_iterator it = cats.begin();
	 it != cats.end(); it++) {
	QRadioButton *but = new QRadioButton(catgBGRP);
	QString catgnm = QString::fromUtf8(it->c_str(), it->length());
	m_catgbutvec.push_back(*it);
	// We strip text before the first colon before setting the button name.
	// This is so that the user can decide the order of buttons by naming 
	// the filter,ie, a:media b:messages etc.
	QString but_txt = catgnm;
	int colon = catgnm.indexOf(':');
	if (colon != -1) {
	    but_txt = catgnm.right(catgnm.size()-(colon+1));
	}
	but->setText(tr(but_txt.toUtf8()));
	if (prefs.catgToolBar && catgCMB)
	    catgCMB->addItem(tr(catgnm.toUtf8()));
        bgrphbox->addWidget(but);
        bgrp->addButton(but, bgrpid++);
    }
    catgBGRP->setLayout(bgrphbox);

    if (prefs.catgToolBar)
	catgBGRP->hide();

    sSearch->queryText->installEventFilter(this);

    restable = new ResTable(this);
    verticalLayout->insertWidget(2, restable);
    actionShowResultsAsTable->setChecked(prefs.showResultsAsTable);
    on_actionShowResultsAsTable_toggled(prefs.showResultsAsTable);

    // Must not do this when restable is a child of rclmain
    // sc = new QShortcut(quitKeySeq, restable);
    // connect(sc, SIGNAL (activated()), this, SLOT (fileExit()));

    // A shortcut to get the focus back to the search entry. 
    QKeySequence seq("Ctrl+Shift+s");
    QShortcut *sc = new QShortcut(seq, this);
    connect(sc, SIGNAL (activated()), 
	    this, SLOT (focusToSearch()));

    connect(&m_watcher, SIGNAL(fileChanged(QString)), 
	    this, SLOT(idxStatus()));
    connect(sSearch, SIGNAL(startSearch(RefCntr<Rcl::SearchData>)), 
	    this, SLOT(startSearch(RefCntr<Rcl::SearchData>)));
    connect(sSearch, SIGNAL(clearSearch()), 
	    this, SLOT(resetSearch()));

    connect(preferencesMenu, SIGNAL(triggered(QAction*)),
	    this, SLOT(setStemLang(QAction*)));
    connect(preferencesMenu, SIGNAL(aboutToShow()),
	    this, SLOT(adjustPrefsMenu()));
    connect(fileExitAction, SIGNAL(activated() ), 
	    this, SLOT(fileExit() ) );
    connect(fileToggleIndexingAction, SIGNAL(activated()), 
	    this, SLOT(toggleIndexing()));
    connect(fileRebuildIndexAction, SIGNAL(activated()), 
	    this, SLOT(rebuildIndex()));
    connect(fileEraseDocHistoryAction, SIGNAL(activated()), 
	    this, SLOT(eraseDocHistory()));
    connect(fileEraseSearchHistoryAction, SIGNAL(activated()), 
	    this, SLOT(eraseSearchHistory()));
    connect(helpAbout_RecollAction, SIGNAL(activated()), 
	    this, SLOT(showAboutDialog()));
    connect(showMissingHelpers_Action, SIGNAL(activated()), 
	    this, SLOT(showMissingHelpers()));
    connect(showActiveTypes_Action, SIGNAL(activated()), 
	    this, SLOT(showActiveTypes()));
    connect(userManualAction, SIGNAL(activated()), 
	    this, SLOT(startManual()));
    connect(toolsDoc_HistoryAction, SIGNAL(activated()), 
	    this, SLOT(showDocHistory()));
    connect(toolsAdvanced_SearchAction, SIGNAL(activated()), 
	    this, SLOT(showAdvSearchDialog()));
    connect(toolsSpellAction, SIGNAL(activated()), 
	    this, SLOT(showSpellDialog()));
    connect(indexConfigAction, SIGNAL(activated()), 
	    this, SLOT(showIndexConfig()));
    connect(indexScheduleAction, SIGNAL(activated()), 
	    this, SLOT(showIndexSched()));
    connect(queryPrefsAction, SIGNAL(activated()), 
	    this, SLOT(showUIPrefs()));
    connect(extIdxAction, SIGNAL(activated()), 
	    this, SLOT(showExtIdxDialog()));

    if (prefs.catgToolBar && catgCMB)
	connect(catgCMB, SIGNAL(activated(int)), 
		this, SLOT(catgFilter(int)));
    connect(toggleFullScreenAction, SIGNAL(activated()), 
            this, SLOT(toggleFullScreen()));
    connect(actionShowQueryDetails, SIGNAL(activated()),
	    this, SLOT(showQueryDetails()));
    connect(periodictimer, SIGNAL(timeout()), 
	    this, SLOT(periodic100()));
    connect(this, SIGNAL(docSourceChanged(RefCntr<DocSequence>)),
	    restable, SLOT(setDocSource(RefCntr<DocSequence>)));
    connect(this, SIGNAL(searchReset()), 
	    restable, SLOT(resetSource()));
    connect(this, SIGNAL(resultsReady()), 
	    restable, SLOT(readDocSource()));
    connect(this, SIGNAL(sortDataChanged(DocSeqSortSpec)), 
	    restable, SLOT(onSortDataChanged(DocSeqSortSpec)));

    connect(restable->getModel(), SIGNAL(sortDataChanged(DocSeqSortSpec)),
	    this, SLOT(onSortDataChanged(DocSeqSortSpec)));
    connect(restable, SIGNAL(docEditClicked(Rcl::Doc)), 
	    this, SLOT(startNativeViewer(Rcl::Doc)));
    connect(restable, SIGNAL(docPreviewClicked(int, Rcl::Doc, int)), 
	    this, SLOT(startPreview(int, Rcl::Doc, int)));
    connect(restable, SIGNAL(docExpand(Rcl::Doc)), 
	    this, SLOT(docExpand(Rcl::Doc)));
    connect(restable, SIGNAL(previewRequested(Rcl::Doc)), 
	    this, SLOT(startPreview(Rcl::Doc)));
    connect(restable, SIGNAL(editRequested(Rcl::Doc)), 
	    this, SLOT(startNativeViewer(Rcl::Doc)));
    connect(restable, SIGNAL(docSaveToFileClicked(Rcl::Doc)), 
	    this, SLOT(saveDocToFile(Rcl::Doc)));

    connect(this, SIGNAL(docSourceChanged(RefCntr<DocSequence>)),
	    reslist, SLOT(setDocSource(RefCntr<DocSequence>)));
    connect(firstPageAction, SIGNAL(activated()), 
	    reslist, SLOT(resultPageFirst()));
    connect(prevPageAction, SIGNAL(activated()), 
	    reslist, SLOT(resPageUpOrBack()));
    connect(nextPageAction, SIGNAL(activated()),
	    reslist, SLOT(resPageDownOrNext()));
    connect(this, SIGNAL(searchReset()), 
	    reslist, SLOT(resetList()));
    connect(this, SIGNAL(resultsReady()), 
	    reslist, SLOT(readDocSource()));

    connect(reslist, SIGNAL(hasResults(int)), 
	    this, SLOT(resultCount(int)));
    connect(reslist, SIGNAL(docExpand(Rcl::Doc)), 
	    this, SLOT(docExpand(Rcl::Doc)));
    connect(reslist, SIGNAL(wordSelect(QString)),
	    sSearch, SLOT(addTerm(QString)));
    connect(reslist, SIGNAL(wordReplace(const QString&, const QString&)),
	    sSearch, SLOT(onWordReplace(const QString&, const QString&)));
    connect(reslist, SIGNAL(nextPageAvailable(bool)), 
	    this, SLOT(enableNextPage(bool)));
    connect(reslist, SIGNAL(prevPageAvailable(bool)), 
	    this, SLOT(enablePrevPage(bool)));
    connect(reslist, SIGNAL(docEditClicked(Rcl::Doc)), 
	    this, SLOT(startNativeViewer(Rcl::Doc)));
    connect(reslist, SIGNAL(docSaveToFileClicked(Rcl::Doc)), 
	    this, SLOT(saveDocToFile(Rcl::Doc)));
    connect(reslist, SIGNAL(editRequested(Rcl::Doc)), 
	    this, SLOT(startNativeViewer(Rcl::Doc)));
    connect(reslist, SIGNAL(docPreviewClicked(int, Rcl::Doc, int)), 
	    this, SLOT(startPreview(int, Rcl::Doc, int)));
    connect(reslist, SIGNAL(previewRequested(Rcl::Doc)), 
	    this, SLOT(startPreview(Rcl::Doc)));
    connect(reslist, SIGNAL(headerClicked()), 
	    this, SLOT(showQueryDetails()));

    if (prefs.keepSort && prefs.sortActive) {
	m_sortspec.field = (const char *)prefs.sortField.toUtf8();
	m_sortspec.desc = prefs.sortDesc;
	onSortDataChanged(m_sortspec);
	emit sortDataChanged(m_sortspec);
    }

    fileRebuildIndexAction->setEnabled(FALSE);
    fileToggleIndexingAction->setEnabled(FALSE);
    // Start timer on a slow period (used for checking ^C). Will be
    // speeded up during indexing
    periodictimer->start(1000);
}

void RclMain::resultCount(int n)
{
    actionSortByDateAsc->setEnabled(n>0);
    actionSortByDateDesc->setEnabled(n>0);
}

// This is called by a timer right after we come up. Try to open
// the database and talk to the user if we can't
void RclMain::initDbOpen()
{
    bool nodb = false;
    string reason;
    bool maindberror;
    if (!maybeOpenDb(reason, true, &maindberror)) {
        nodb = true;
	if (maindberror) {
	    FirstIdxDialog fidia(this);
	    connect(fidia.idxconfCLB, SIGNAL(clicked()), 
		    this, SLOT(execIndexConfig()));
	    connect(fidia.idxschedCLB, SIGNAL(clicked()), 
		    this, SLOT(execIndexSched()));
	    connect(fidia.runidxPB, SIGNAL(clicked()), 
		    this, SLOT(toggleIndexing()));
	    fidia.exec();
	    // Don't open adv search or run cmd line search in this case.
	    return;
	} else {
	    QMessageBox::warning(0, "Recoll", 
				 tr("Could not open external index. Db not open. Check external indexes list."));
	}
    }

    if (prefs.startWithAdvSearchOpen)
	showAdvSearchDialog();
    // If we have something in the search entry, it comes from a
    // command line argument
    if (!nodb && sSearch->hasSearchString())
	QTimer::singleShot(0, sSearch, SLOT(startSimpleSearch()));
}

void RclMain::focusToSearch()
{
    LOGDEB(("Giving focus to sSearch\n"));
    sSearch->queryText->setFocus(Qt::ShortcutFocusReason);
}

void RclMain::setStemLang(QAction *id)
{
    LOGDEB(("RclMain::setStemLang(%p)\n", id));
    // Check that the menu entry is for a stemming language change
    // (might also be "show prefs" etc.
    bool isLangId = false;
    for (map<QString, QAction*>::const_iterator it = m_stemLangToId.begin();
	 it != m_stemLangToId.end(); it++) {
	if (id == it->second)
	    isLangId = true;
    }
    if (!isLangId)
	return;

    // Set the "checked" item state for lang entries
    for (map<QString, QAction*>::const_iterator it = m_stemLangToId.begin();
	 it != m_stemLangToId.end(); it++) {
	(it->second)->setChecked(false);
    }
    id->setChecked(true);

    // Retrieve language value (also handle special cases), set prefs,
    // notify that we changed
    QString lang;
    if (id == m_idNoStem) {
	lang = "";
    } else if (id == m_idAllStem) {
	lang = "ALL";
    } else {
	lang = id->text();
    }
    prefs.queryStemLang = lang;
    LOGDEB(("RclMain::setStemLang(%d): lang [%s]\n", 
	    id, (const char *)prefs.queryStemLang.toAscii()));
    rwSettings(true);
    emit stemLangChanged(lang);
}

// Set the checked stemming language item before showing the prefs menu
void RclMain::setStemLang(const QString& lang)
{
    LOGDEB(("RclMain::setStemLang(%s)\n", (const char *)lang.toAscii()));
    QAction *id;
    if (lang == "") {
	id = m_idNoStem;
    } else if (lang == "ALL") {
	id = m_idAllStem;
    } else {
	map<QString, QAction*>::iterator it = m_stemLangToId.find(lang);
	if (it == m_stemLangToId.end()) 
	    return;
	id = it->second;
    }
    for (map<QString, QAction*>::const_iterator it = m_stemLangToId.begin();
	 it != m_stemLangToId.end(); it++) {
	(it->second)->setChecked(false);
    }
    id->setChecked(true);
}

// Prefs menu about to show
void RclMain::adjustPrefsMenu()
{
    setStemLang(prefs.queryStemLang);
}

void RclMain::closeEvent( QCloseEvent * )
{
    fileExit();
}

// We also want to get rid of the advanced search form and previews
// when we exit (not our children so that it's not systematically
// created over the main form). 
bool RclMain::close()
{
    LOGDEB(("RclMain::close\n"));
    fileExit();
    return false;
}

void RclMain::fileExit()
{
    LOGDEB(("RclMain: fileExit\n"));
    // Don't save geometry if we're currently fullscreened
    if (!isFullScreen()) {
        prefs.mainwidth = width();
        prefs.mainheight = height();
    }
    restable->saveColState();

    prefs.ssearchTyp = sSearch->searchTypCMB->currentIndex();
    if (asearchform)
	delete asearchform;

    // Let the exit handler clean up the rest (internal recoll stuff).
    exit(0);
}

void RclMain::idxStatus()
{
    ConfSimple cs(theconfig->getIdxStatusFile().c_str(), 1);
    QString msg = tr("Indexing in progress: ");
    DbIxStatus status;
    string val;
    cs.get("phase", val);
    status.phase = DbIxStatus::Phase(atoi(val.c_str()));
    cs.get("fn", status.fn);
    cs.get("docsdone", val);
    status.docsdone = atoi(val.c_str());
    cs.get("filesdone", val);
    status.filesdone = atoi(val.c_str());
    cs.get("dbtotdocs", val);
    status.dbtotdocs = atoi(val.c_str());

    QString phs;
    switch (status.phase) {
    case DbIxStatus::DBIXS_NONE:phs=tr("None");break;
    case DbIxStatus::DBIXS_FILES: phs=tr("Updating");break;
    case DbIxStatus::DBIXS_PURGE: phs=tr("Purge");break;
    case DbIxStatus::DBIXS_STEMDB: phs=tr("Stemdb");break;
    case DbIxStatus::DBIXS_CLOSING:phs=tr("Closing");break;
    case DbIxStatus::DBIXS_DONE:phs=tr("Done");break;
    case DbIxStatus::DBIXS_MONITOR:phs=tr("Monitor");break;
    default: phs=tr("Unknown");break;
    }
    msg += phs + " ";
    if (status.phase == DbIxStatus::DBIXS_FILES) {
	char cnts[100];
	if (status.dbtotdocs > 0)
	    sprintf(cnts,"(%d/%d/%d) ", status.docsdone, status.filesdone, 
		    status.dbtotdocs);
	else
	    sprintf(cnts, "(%d/%d) ", status.docsdone, status.filesdone);
	msg += QString::fromAscii(cnts) + " ";
    }
    string mf;int ecnt = 0;
    string fcharset = theconfig->getDefCharset(true);
    if (!transcode(status.fn, mf, fcharset, "UTF-8", &ecnt) || ecnt) {
	mf = url_encode(status.fn, 0);
    }
    msg += QString::fromUtf8(mf.c_str());
    statusBar()->showMessage(msg, 4000);
}

// This is called by a periodic timer to check the status of 
// indexing, a possible need to exit, and cleanup exited viewers
void RclMain::periodic100()
{
    LOGDEB2(("Periodic100\n"));
    if (m_idxproc) {
	// An indexing process was launched. If its' done, see status.
	int status;
	bool exited = m_idxproc->maybereap(&status);
	if (exited) {
	    deleteZ(m_idxproc);
	    if (status) {
		QMessageBox::warning(0, "Recoll", 
				     tr("Indexing failed"));
	    }
	    string reason;
	    maybeOpenDb(reason, 1);
	} else {
	    // update/show status even if the status file did not
	    // change (else the status line goes blank during
	    // lengthy operations).
	    idxStatus();
	}
    }
    // Update the "start/stop indexing" menu entry, can't be done from
    // the "start/stop indexing" slot itself
    if (m_idxproc) {
	m_indexerState = IXST_RUNNINGMINE;
	fileToggleIndexingAction->setText(tr("Stop &Indexing"));
	fileToggleIndexingAction->setEnabled(TRUE);
	fileRebuildIndexAction->setEnabled(FALSE);
	periodictimer->setInterval(200);
    } else {
	Pidfile pidfile(theconfig->getPidfile());
	if (pidfile.open() == 0) {
	    m_indexerState = IXST_NOTRUNNING;
	    fileToggleIndexingAction->setText(tr("Update &Index"));
	    fileToggleIndexingAction->setEnabled(TRUE);
	    fileRebuildIndexAction->setEnabled(TRUE);
	    periodictimer->setInterval(1000);
	} else {
	    // Real time or externally started batch indexer running
	    m_indexerState = IXST_RUNNINGNOTMINE;
	    fileToggleIndexingAction->setText(tr("Stop &Indexing"));
	    fileToggleIndexingAction->setEnabled(TRUE);
	    fileRebuildIndexAction->setEnabled(FALSE);
	    periodictimer->setInterval(200);
	}	    
    }

    // Possibly cleanup the dead viewers
    for (vector<ExecCmd*>::iterator it = m_viewers.begin();
	 it != m_viewers.end(); it++) {
	int status;
	if ((*it)->maybereap(&status)) {
	    deleteZ(*it);
	}
    }
    vector<ExecCmd*> v;
    for (vector<ExecCmd*>::iterator it = m_viewers.begin();
	 it != m_viewers.end(); it++) {
	if (*it)
	    v.push_back(*it);
    }
    m_viewers = v;

    if (recollNeedsExit)
	fileExit();
}

// This gets called when the "update iindex" action is activated. It executes
// the requested action, and disables the menu entry. This will be
// re-enabled by the indexing status check
void RclMain::toggleIndexing()
{
    switch (m_indexerState) {
    case IXST_RUNNINGMINE:
	if (m_idxproc) {
	    // Indexing was in progress, request stop. Let the periodic
	    // routine check for the results.
	    int pid = m_idxproc->getChildPid();
	    if (pid > 0)
		kill(pid, SIGTERM);
	}
	break;
    case IXST_RUNNINGNOTMINE:
    {
	int rep = 
	    QMessageBox::information(0, tr("Warning"), 
				     tr("The current indexing process "
					"was not started from this "
					"interface. Click Ok to kill it "
					"anyway, or Cancel to leave it alone"),
					 QMessageBox::Ok,
					 QMessageBox::Cancel,
					 QMessageBox::NoButton);
	if (rep == QMessageBox::Ok) {
	    Pidfile pidfile(theconfig->getPidfile());
	    pid_t pid = pidfile.open();
	    if (pid > 0)
		kill(pid, SIGTERM);
	}
    }		
    break;
    case IXST_NOTRUNNING:
    {
	list<string> args;
	args.push_back("-c");
	args.push_back(theconfig->getConfDir());
	m_idxproc = new ExecCmd;
	m_idxproc->startExec("recollindex", args, false, false);
    }
    break;
    }
}

void RclMain::rebuildIndex()
{
    switch (m_indexerState) {
    case IXST_RUNNINGMINE:
    case IXST_RUNNINGNOTMINE:
	return; //?? Should not have been called
    case IXST_NOTRUNNING:
    {
	int rep = 
	    QMessageBox::warning(0, tr("Erasing index"), 
				     tr("Reset the index and start "
					"from scratch ?"),
					 QMessageBox::Ok,
					 QMessageBox::Cancel,
					 QMessageBox::NoButton);
	if (rep == QMessageBox::Ok) {
	    list<string> args;
	    args.push_back("-c");
	    args.push_back(theconfig->getConfDir());
	    args.push_back("-z");
	    m_idxproc = new ExecCmd;
	    m_idxproc->startExec("recollindex", args, false, false);
	}
    }
    break;
    }
}

// Start a db query and set the reslist docsource
void RclMain::startSearch(RefCntr<Rcl::SearchData> sdata)
{
    LOGDEB(("RclMain::startSearch. Indexing %s\n", m_idxproc?"on":"off"));
    emit searchReset();
    m_source = RefCntr<DocSequence>();

    // The db may have been closed at the end of indexing
    string reason;
    // If indexing is being performed, we reopen the db at each query.
    if (!maybeOpenDb(reason, m_idxproc != 0)) {
	QMessageBox::critical(0, "Recoll", QString(reason.c_str()));
	return;
    }

    string stemLang = (const char *)prefs.queryStemLang.toAscii();
    if (stemLang == "ALL") {
	theconfig->getConfParam("indexstemminglanguages", stemLang);
    }
    sdata->setStemlang(stemLang);

    Rcl::Query *query = new Rcl::Query(rcldb);
    query->setCollapseDuplicates(prefs.collapseDuplicates);

    curPreview = 0;
    DocSequenceDb *src = 
	new DocSequenceDb(RefCntr<Rcl::Query>(query), 
			  string(tr("Query results").toUtf8()), sdata);
    src->setAbstractParams(prefs.queryBuildAbstract, 
                           prefs.queryReplaceAbstract);
    m_source = RefCntr<DocSequence>(src);
    m_source->setSortSpec(m_sortspec);
    m_source->setFiltSpec(m_filtspec);

    emit docSourceChanged(m_source);
    emit sortDataChanged(m_sortspec);
    initiateQuery();
}

class QueryThread : public QThread {
    int loglevel;
    RefCntr<DocSequence> m_source;
 public: 
    QueryThread(RefCntr<DocSequence> source)
	: m_source(source)
    {
	loglevel = DebugLog::getdbl()->getlevel();
    }
    ~QueryThread() { }
    virtual void run() 
    {
	DebugLog::getdbl()->setloglevel(loglevel);
	cnt = m_source->getResCnt();
    }
    int cnt;
};

void RclMain::initiateQuery()
{
    if (m_source.isNull())
	return;

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    QueryThread qthr(m_source);
    qthr.start();

    QProgressDialog progress(this);
    progress.setLabelText(tr("Query in progress.<br>"
			     "Due to limitations of the indexing library,<br>"
			     "cancelling will exit the program"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setRange(0,0);

    // For some reason setMinimumDuration() does not seem to work with
    // a busy dialog (range 0,0) Have to call progress.show() inside
    // the loop.
    // progress.setMinimumDuration(2000);
    // Also the multiple processEvents() seem to improve the responsiveness??
    for (int i = 0;;i++) {
	qApp->processEvents();
	if (qthr.wait(100)) {
	    break;
	}
	if (i == 20)
	    progress.show();
	qApp->processEvents();
	if (progress.wasCanceled()) {
	    fileExit();
	}

	qApp->processEvents();
    }

    int cnt = qthr.cnt;
    QString msg;
    if (cnt > 0) {
	QString str;
	msg = tr("Result count (est.)") + ": " + 
	    str.setNum(cnt);
    } else {
	msg = tr("No results found");
    }

    statusBar()->showMessage(msg, 0);
    QApplication::restoreOverrideCursor();
    emit(resultsReady());
}

void RclMain::resetSearch()
{
    emit searchReset();
}

// Open advanced search dialog.
void RclMain::showAdvSearchDialog()
{
    if (asearchform == 0) {
	asearchform = new AdvSearch(0);
	connect(new QShortcut(quitKeySeq, asearchform), SIGNAL (activated()), 
		this, SLOT (fileExit()));

	connect(asearchform, SIGNAL(startSearch(RefCntr<Rcl::SearchData>)), 
		this, SLOT(startSearch(RefCntr<Rcl::SearchData>)));
	asearchform->show();
    } else {
	// Close and reopen, in hope that makes us visible...
	asearchform->close();
	asearchform->show();
    }
}

void RclMain::showSpellDialog()
{
    if (spellform == 0) {
	spellform = new SpellW(0);
	connect(new QShortcut(quitKeySeq, spellform), SIGNAL (activated()), 
		this, SLOT (fileExit()));
	connect(spellform, SIGNAL(wordSelect(QString)),
		sSearch, SLOT(addTerm(QString)));
	spellform->show();
    } else {
	// Close and reopen, in hope that makes us visible...
	spellform->close();
        spellform->show();
    }

}

void RclMain::showIndexConfig()
{
    showIndexConfig(false);
}
void RclMain::execIndexConfig()
{
    showIndexConfig(true);
}
void RclMain::showIndexConfig(bool modal)
{
    LOGDEB(("showIndexConfig()\n"));
    if (indexConfig == 0) {
	indexConfig = new ConfIndexW(0, theconfig);
	connect(new QShortcut(quitKeySeq, indexConfig), SIGNAL (activated()), 
		this, SLOT (fileExit()));
    } else {
	// Close and reopen, in hope that makes us visible...
	indexConfig->close();
	indexConfig->reloadPanels();
    }
    if (modal) {
	indexConfig->exec();
	indexConfig->setModal(false);
    } else {
	indexConfig->show();
    }
}

void RclMain::showIndexSched()
{
    showIndexSched(false);
}
void RclMain::execIndexSched()
{
    showIndexSched(true);
}
void RclMain::showIndexSched(bool modal)
{
    LOGDEB(("showIndexSched()\n"));
    if (indexSched == 0) {
	indexSched = new IdxSchedW(this);
	connect(new QShortcut(quitKeySeq, indexSched), SIGNAL (activated()), 
		this, SLOT (fileExit()));
	connect(indexSched->cronCLB, SIGNAL(clicked()), 
		this, SLOT(execCronTool()));
	if (theconfig && theconfig->isDefaultConfig()) {
	    connect(indexSched->rtidxCLB, SIGNAL(clicked()), 
		    this, SLOT(execRTITool()));
	} else {
	    indexSched->rtidxCLB->setEnabled(false);
	}
    } else {
	// Close and reopen, in hope that makes us visible...
	indexSched->close();
    }
    if (modal) {
	indexSched->exec();
	indexSched->setModal(false);
    } else {
	indexSched->show();
    }
}

void RclMain::showCronTool()
{
    showCronTool(false);
}
void RclMain::execCronTool()
{
    showCronTool(true);
}
void RclMain::showCronTool(bool modal)
{
    LOGDEB(("showCronTool()\n"));
    if (cronTool == 0) {
	cronTool = new CronToolW(0);
	connect(new QShortcut(quitKeySeq, cronTool), SIGNAL (activated()), 
		this, SLOT (fileExit()));
    } else {
	// Close and reopen, in hope that makes us visible...
	cronTool->close();
    }
    if (modal) {
	cronTool->exec();
	cronTool->setModal(false);
    } else {
	cronTool->show();
    }
}

void RclMain::showRTITool()
{
    showRTITool(false);
}
void RclMain::execRTITool()
{
    showRTITool(true);
}
void RclMain::showRTITool(bool modal)
{
    LOGDEB(("showRTITool()\n"));
    if (rtiTool == 0) {
	rtiTool = new RTIToolW(0);
	connect(new QShortcut(quitKeySeq, rtiTool), SIGNAL (activated()), 
		this, SLOT (fileExit()));
    } else {
	// Close and reopen, in hope that makes us visible...
	rtiTool->close();
    }
    if (modal) {
	rtiTool->exec();
	rtiTool->setModal(false);
    } else {
	rtiTool->show();
    }
}

void RclMain::showUIPrefs()
{
    if (uiprefs == 0) {
	uiprefs = new UIPrefsDialog(0);
	connect(new QShortcut(quitKeySeq, uiprefs), SIGNAL (activated()), 
		this, SLOT (fileExit()));
	connect(uiprefs, SIGNAL(uiprefsDone()), this, SLOT(setUIPrefs()));
	connect(this, SIGNAL(stemLangChanged(const QString&)), 
		uiprefs, SLOT(setStemLang(const QString&)));
    } else {
	// Close and reopen, in hope that makes us visible...
	uiprefs->close();
    }
    uiprefs->show();
}

void RclMain::showExtIdxDialog()
{
    if (uiprefs == 0) {
	uiprefs = new UIPrefsDialog(0);
	connect(new QShortcut(quitKeySeq, uiprefs), SIGNAL (activated()), 
		this, SLOT (fileExit()));
	connect(uiprefs, SIGNAL(uiprefsDone()), this, SLOT(setUIPrefs()));
    } else {
	// Close and reopen, in hope that makes us visible...
	uiprefs->close();
    }
    uiprefs->tabWidget->setCurrentIndex(3);
    uiprefs->show();
}

void RclMain::showAboutDialog()
{
    string vstring = Rcl::version_string() +
        string("<br> http://www.recoll.org") +
	string("<br> http://www.xapian.org");
    QMessageBox::information(this, tr("About Recoll"), vstring.c_str());
}

void RclMain::showMissingHelpers()
{
    string miss = theconfig->getMissingHelperDesc();
    QString msg = tr("External applications/commands needed and not found "
		     "for indexing your file types:\n\n");
    if (!miss.empty()) {
	msg += QString::fromUtf8(miss.c_str());
    } else {
	msg += tr("No helpers found missing");
    }
    QMessageBox::information(this, tr("Missing helper programs"), msg);
}

void RclMain::showActiveTypes()
{
    if (rcldb == 0) {
	QMessageBox::warning(0, tr("Error"), 
			     tr("Index not open"),
			     QMessageBox::Ok, 
			     QMessageBox::NoButton);
	return;
    }

    // Get list of all mime types in index. For this, we use a
    // wildcard field search on mtype
    Rcl::TermMatchResult matches;
    string prefix;
    if (!rcldb->termMatch(Rcl::Db::ET_WILD, "", "*", matches, -1, "mtype", 
			  &prefix)) {
	QMessageBox::warning(0, tr("Error"), 
			     tr("Index query error"),
			     QMessageBox::Ok, 
			     QMessageBox::NoButton);
	return;
    }

    // Build the set of mtypes, stripping the prefix
    set<string> mtypesfromdb;
    for (list<Rcl::TermMatchEntry>::const_iterator it = matches.entries.begin(); 
	 it != matches.entries.end(); it++) {
	mtypesfromdb.insert(it->term.substr(prefix.size()));
    }

    // All types listed in mimeconf:
    list<string> mtypesfromconfig = theconfig->getAllMimeTypes();

    // Intersect file system types with config types (those not in the
    // config can be indexed by name, not by content)
    set<string> mtypesfromdbconf;
    for (list<string>::const_iterator it = mtypesfromconfig.begin();
	 it != mtypesfromconfig.end(); it++) {
	if (mtypesfromdb.find(*it) != mtypesfromdb.end())
	    mtypesfromdbconf.insert(*it);
    }

    // Substract the types for missing helpers (the docs are indexed by name only):
    string miss = theconfig->getMissingHelperDesc();
    if (!miss.empty()) {
	FIMissingStore st;
	FileInterner::getMissingFromDescription(&st, miss);
	map<string, set<string> >::const_iterator it;
	for (it = st.m_typesForMissing.begin(); 
	     it != st.m_typesForMissing.end(); it++) {
	    set<string>::const_iterator it1;
	    for (it1 = it->second.begin(); 
		 it1 != it->second.end(); it1++) {
		set<string>::iterator it2 = mtypesfromdbconf.find(*it1);
		if (it2 != mtypesfromdbconf.end())
		    mtypesfromdbconf.erase(it2);
	    }
	}	
    }
    ListDialog dialog;
    dialog.setWindowTitle(tr("Indexed MIME Types"));

    // Turn the result into a string and display
    dialog.groupBox->setTitle(tr("Content has been indexed for these mime types:"));

    // We replace the list with an editor so that the user can copy/paste
    delete dialog.listWidget;
    QTextEdit *editor = new QTextEdit(dialog.groupBox);
    editor->setReadOnly(TRUE);
    dialog.horizontalLayout->addWidget(editor);

    for (set<string>::const_iterator it = mtypesfromdbconf.begin(); 
	 it != mtypesfromdbconf.end(); it++) {
	editor->append(QString::fromAscii(it->c_str()));
    }
    editor->moveCursor(QTextCursor::Start);
    editor->ensureCursorVisible();
    dialog.exec();
}

// If a preview (toplevel) window gets closed by the user, we need to
// clean up because there is no way to reopen it. And check the case
// where the current one is closed
void RclMain::previewClosed(Preview *w)
{
    LOGDEB(("RclMain::previewClosed(%p)\n", w));
    if (w == curPreview) {
	LOGDEB(("Active preview closed\n"));
	curPreview = 0;
    } else {
	LOGDEB(("Old preview closed\n"));
    }
    delete w;
}

/** 
 * Open a preview window for a given document, or load it into new tab of 
 * existing window.
 *
 * @param docnum db query index
 * @param mod keyboards modifiers like ControlButton, ShiftButton
 */
void RclMain::startPreview(int docnum, Rcl::Doc doc, int mod)
{
    LOGDEB(("startPreview(%d, doc, %d)\n", docnum, mod));

    // Document up to date check. We do this only if ipath is not
    // empty as this does not appear to be a serious issue for single
    // docs (the main actual problem is displaying the wrong message
    // from a compacted mail folder)
    //
    // !! NOTE: there is one case where doing a partial index update
    // will not worl: if the search result does not exist in the new
    // version of the file, it won't be purged from the index because
    // a partial index pass does no purge, so its ref date will stay
    // the same and you keep getting the message about the index being
    // out of date. The only way to fix this is to run a normal
    // indexing pass. 
    // Also we should re-run the query after updating the index
    // because the ipaths may be wrong in the current result list
    if (!doc.ipath.empty()) {
	string udi, sig;
	doc.getmeta(Rcl::Doc::keyudi, &udi);
	FileInterner::makesig(doc, sig);
	if (rcldb && !udi.empty()) {
	    if (rcldb->needUpdate(udi, sig)) {
		int rep = 
		    QMessageBox::warning(0, tr("Warning"), 
				       tr("Index not up to date for this file. "
					"Refusing to risk showing the wrong "
					"entry. Click Ok to update the "
					"index for this file, then re-run the "
					"query when indexing is done. "
					"Else, Cancel."),
					 QMessageBox::Ok,
					 QMessageBox::Cancel,
					 QMessageBox::NoButton);
		if (rep == QMessageBox::Ok) {
		    LOGDEB(("Requesting index update for %s\n", 
			    doc.url.c_str()));
		    vector<Rcl::Doc> docs(1, doc);
		    updateIdxForDocs(docs);
		}
		return;
	    }
	}
    }

    if (mod & Qt::ShiftModifier) {
	// User wants new preview window
	curPreview = 0;
    }
    if (curPreview == 0) {
	HiliteData hdata;
	m_source->getTerms(hdata.terms, hdata.groups, hdata.gslks);
	curPreview = new Preview(reslist->listId(), hdata);

	if (curPreview == 0) {
	    QMessageBox::warning(0, tr("Warning"), 
				 tr("Can't create preview window"),
				 QMessageBox::Ok, 
				 QMessageBox::NoButton);
	    return;
	}
	connect(new QShortcut(quitKeySeq, curPreview), SIGNAL (activated()), 
		this, SLOT (fileExit()));
	connect(curPreview, SIGNAL(previewClosed(Preview *)), 
		this, SLOT(previewClosed(Preview *)));
	connect(curPreview, SIGNAL(wordSelect(QString)),
		sSearch, SLOT(addTerm(QString)));
	connect(curPreview, SIGNAL(showNext(Preview *, int, int)),
		this, SLOT(previewNextInTab(Preview *, int, int)));
	connect(curPreview, SIGNAL(showPrev(Preview *, int, int)),
		this, SLOT(previewPrevInTab(Preview *, int, int)));
	connect(curPreview, SIGNAL(previewExposed(Preview *, int, int)),
		this, SLOT(previewExposed(Preview *, int, int)));
	connect(curPreview, SIGNAL(saveDocToFile(Rcl::Doc)), 
		this, SLOT(saveDocToFile(Rcl::Doc)));
	curPreview->setWindowTitle(getQueryDescription());
	curPreview->show();
    } 
    curPreview->makeDocCurrent(doc, docnum);
}

void RclMain::updateIdxForDocs(vector<Rcl::Doc>& docs)
{
    if (m_idxproc) {
	QMessageBox::warning(0, tr("Warning"), 
			     tr("Can't update index: indexer running"),
			     QMessageBox::Ok, 
			     QMessageBox::NoButton);
	return;
    }
	
    vector<string> paths;
    if (ConfIndexer::docsToPaths(docs, paths)) {
	list<string> args;
	args.push_back("-c");
	args.push_back(theconfig->getConfDir());
	args.push_back("-i");
	args.insert(args.end(), paths.begin(), paths.end());
	m_idxproc = new ExecCmd;
	m_idxproc->startExec("recollindex", args, false, false);
	fileToggleIndexingAction->setText(tr("Stop &Indexing"));
    }
    fileToggleIndexingAction->setEnabled(FALSE);
}

/** 
 * Open a preview window for a given document, no linking to result list
 *
 * This is used to show ie parent documents, which have no corresponding
 * entry in the result list.
 * 
 */
void RclMain::startPreview(Rcl::Doc doc)
{
    Preview *preview = new Preview(0, HiliteData());
    if (preview == 0) {
	QMessageBox::warning(0, tr("Warning"), 
			     tr("Can't create preview window"),
			     QMessageBox::Ok, 
			     QMessageBox::NoButton);
	return;
    }
    connect(new QShortcut(quitKeySeq, preview), SIGNAL (activated()), 
	    this, SLOT (fileExit()));
    connect(preview, SIGNAL(wordSelect(QString)),
	    sSearch, SLOT(addTerm(QString)));
    preview->show();
    preview->makeDocCurrent(doc, 0);
}

// Show next document from result list in current preview tab
void RclMain::previewNextInTab(Preview * w, int sid, int docnum)
{
    previewPrevOrNextInTab(w, sid, docnum, true);
}

// Show previous document from result list in current preview tab
void RclMain::previewPrevInTab(Preview * w, int sid, int docnum)
{
    previewPrevOrNextInTab(w, sid, docnum, false);
}

// Combined next/prev from result list in current preview tab
void RclMain::previewPrevOrNextInTab(Preview * w, int sid, int docnum, bool nxt)
{
    LOGDEB(("RclMain::previewNextInTab  sid %d docnum %d, listId %d\n", 
	    sid, docnum, reslist->listId()));

    if (w == 0) // ??
	return;

    if (sid != reslist->listId()) {
	QMessageBox::warning(0, "Recoll", 
			     tr("This search is not active any more"));
	return;
    }

    if (nxt)
	docnum++;
    else 
	docnum--;
    if (docnum < 0 || m_source.isNull() || docnum >= m_source->getResCnt()) {
	QApplication::beep();
	return;
    }

    Rcl::Doc doc;
    if (!reslist->getDoc(docnum, doc)) {
	QMessageBox::warning(0, "Recoll", 
			     tr("Cannot retrieve document info from database"));
	return;
    }
	
    w->makeDocCurrent(doc, docnum, true);
}

// Preview tab exposed: if the preview comes from the currently
// displayed result list, tell reslist (to color the paragraph)
void RclMain::previewExposed(Preview *, int sid, int docnum)
{
    LOGDEB2(("RclMain::previewExposed: sid %d docnum %d, m_sid %d\n", 
	     sid, docnum, reslist->listId()));
    if (sid != reslist->listId()) {
	return;
    }
    reslist->previewExposed(docnum);
}

void RclMain::onSortCtlChanged()
{
    if (m_sortspecnochange)
	return;

    LOGDEB(("RclMain::onSortCtlChanged()\n"));
    m_sortspec.reset();
    if (actionSortByDateAsc->isChecked()) {
	m_sortspec.field = "mtime";
	m_sortspec.desc = false;
	prefs.sortActive = true;
	prefs.sortDesc = false;
	prefs.sortField = "mtime";
    } else if (actionSortByDateDesc->isChecked()) {
	m_sortspec.field = "mtime";
	m_sortspec.desc = true;
	prefs.sortActive = true;
	prefs.sortDesc = true;
	prefs.sortField = "mtime";
    } else {
	prefs.sortActive = prefs.sortDesc = false;
	prefs.sortField = "";
    }
    if (m_source.isNotNull())
	m_source->setSortSpec(m_sortspec);
    emit sortDataChanged(m_sortspec);
    initiateQuery();
}

void RclMain::onSortDataChanged(DocSeqSortSpec spec)
{
    LOGDEB(("RclMain::onSortDataChanged\n"));
    m_sortspecnochange = true;
    if (spec.field.compare("mtime")) {
	actionSortByDateDesc->setChecked(false);
	actionSortByDateAsc->setChecked(false);
    } else {
	actionSortByDateDesc->setChecked(spec.desc);
	actionSortByDateAsc->setChecked(!spec.desc);
    }
    m_sortspecnochange = false;
    if (m_source.isNotNull())
	m_source->setSortSpec(spec);
    m_sortspec = spec;

    prefs.sortField = QString::fromUtf8(spec.field.c_str());
    prefs.sortDesc = spec.desc;
    prefs.sortActive = !spec.field.empty();

    initiateQuery();
}

void RclMain::on_actionShowResultsAsTable_toggled(bool on)
{
    LOGDEB(("RclMain::on_actionShowResultsAsTable_toggled(%d)\n", int(on)));
    prefs.showResultsAsTable = on;
    restable->setVisible(on);
    reslist->setVisible(!on);
    if (!on) {
	displayingTable = false;
	int docnum = restable->getDetailDocNumOrTopRow();
	if (docnum >= 0)
	    reslist->resultPageFor(docnum);
    } else {
	displayingTable = true;
	int docnum = reslist->pageFirstDocNum();
	if (docnum >= 0) {
	    restable->makeRowVisible(docnum);
	}
	nextPageAction->setEnabled(false);
	prevPageAction->setEnabled(false);
	firstPageAction->setEnabled(false);
    }
}

void RclMain::on_actionSortByDateAsc_toggled(bool on)
{
    LOGDEB(("RclMain::on_actionSortByDateAsc_toggled(%d)\n", int(on)));
    if (on) {
	if (actionSortByDateDesc->isChecked()) {
	    actionSortByDateDesc->setChecked(false);
	    // Let our buddy work.
	    return;
	}
    }
    onSortCtlChanged();
}

void RclMain::on_actionSortByDateDesc_toggled(bool on)
{
    LOGDEB(("RclMain::on_actionSortByDateDesc_toggled(%d)\n", int(on)));
    if (on) {
	if (actionSortByDateAsc->isChecked()) {
	    actionSortByDateAsc->setChecked(false);
	    // Let our buddy work.
	    return;
	}
    }
    onSortCtlChanged();
}

void RclMain::saveDocToFile(Rcl::Doc doc)
{
    QString s = 
	QFileDialog::getSaveFileName(this, //parent
				     tr("Save file"), 
				     QString::fromLocal8Bit(path_home().c_str())
	    );
    string tofile((const char *)s.toLocal8Bit());
    TempFile temp; // not used because tofile is set.
    if (!FileInterner::idocToFile(temp, tofile, theconfig, doc)) {
	QMessageBox::warning(0, "Recoll",
			     tr("Cannot extract document or create "
				"temporary file"));
	return;
    }
}

/* Look for html browser. We make a special effort for html because it's
 * used for reading help */
static bool lookForHtmlBrowser(string &exefile)
{
    static const char *htmlbrowserlist = 
	"opera google-chrome konqueror firefox mozilla netscape epiphany";
    vector<string> blist;
    stringToTokens(htmlbrowserlist, blist, " ");

    const char *path = getenv("PATH");
    if (path == 0)
	path = "/bin:/usr/bin:/usr/bin/X11:/usr/X11R6/bin:/usr/local/bin";

    // Look for each browser 
    for (vector<string>::const_iterator bit = blist.begin(); 
	 bit != blist.end(); bit++) {
	if (ExecCmd::which(*bit, exefile, path)) 
	    return true;
    }
    exefile.clear();
    return false;
}

void RclMain::startNativeViewer(Rcl::Doc doc)
{
    // Look for appropriate viewer
    string cmdplusattr;
    if (prefs.useDesktopOpen) {
	cmdplusattr = theconfig->getMimeViewerDef("application/x-all", "");
    } else {
        string apptag;
        doc.getmeta(Rcl::Doc::keyapptg, &apptag);
	cmdplusattr = theconfig->getMimeViewerDef(doc.mimetype, apptag);
    }

    if (cmdplusattr.empty()) {
	QMessageBox::warning(0, "Recoll", 
			     tr("No external viewer configured for mime type [")
			     + doc.mimetype.c_str() + "]");
	return;
    }

    // Extract possible viewer attributes
    ConfSimple attrs;
    string cmd;
    theconfig->valueSplitAttributes(cmdplusattr, cmd, attrs);
    bool ignoreipath = false;
    if (attrs.get("ignoreipath", cmdplusattr))
        ignoreipath = stringToBool(cmdplusattr);

    // Split the command line
    list<string> lcmd;
    if (!stringToStrings(cmd, lcmd)) {
	QMessageBox::warning(0, "Recoll", 
			     tr("Bad viewer command line for %1: [%2]\n"
				"Please check the mimeconf file")
			     .arg(QString::fromAscii(doc.mimetype.c_str()))
			     .arg(QString::fromLocal8Bit(cmd.c_str())));
	return;
    }

    // Look for the command to execute in the exec path and the filters 
    // directory
    string cmdpath;
    if (!ExecCmd::which(lcmd.front(), cmdpath)) {
	cmdpath = theconfig->findFilter(lcmd.front());
	// findFilter returns its input param if the filter is not in
	// the normal places. As we already looked in the path, we
	// have no use for a simple command name here (as opposed to
	// mimehandler which will just let execvp do its thing). Erase
	// cmdpath so that the user dialog will be started further
	// down.
	if (!cmdpath.compare(lcmd.front())) 
	    cmdpath.erase();

	// Specialcase text/html because of the help browser need
	if (cmdpath.empty() && !doc.mimetype.compare("text/html")) {
	    if (lookForHtmlBrowser(cmdpath)) {
		lcmd.clear();
		lcmd.push_back(cmdpath);
		lcmd.push_back("%u");
	    }
	}
    }


    // Command not found: start the user dialog to help find another one:
    if (cmdpath.empty()) {
	QString mt = QString::fromAscii(doc.mimetype.c_str());
	QString message = tr("The viewer specified in mimeview for %1: %2"
			     " is not found.\nDo you want to start the "
			     " preferences dialog ?")
	    .arg(mt).arg(QString::fromLocal8Bit(lcmd.front().c_str()));

	switch(QMessageBox::warning(0, "Recoll", message, 
				    "Yes", "No", 0, 0, 1)) {
	case 0: 
	    showUIPrefs();
	    if (uiprefs)
		uiprefs->showViewAction(mt);
	    break;
	case 1:
	    break;
	}
        // The user will have to click on the link again to try the
        // new command.
	return;
    }

    // We may need a temp file, or not, depending on the command
    // arguments and the fact that this is a subdoc or not.
    bool wantsipath = (cmd.find("%i") != string::npos) || ignoreipath;
    bool wantsfile = cmd.find("%f") != string::npos;
    bool istempfile = false;
    string fn = fileurltolocalpath(doc.url);
    string orgfn = fn;
    string url = doc.url;

    // If the command wants a file but this is not a file url, or
    // there is an ipath that it won't understand, we need a temp file:
    theconfig->setKeyDir(path_getfather(fn));
    if ((wantsfile && fn.empty()) || (!wantsipath && !doc.ipath.empty())) {
	TempFile temp;
	if (!FileInterner::idocToFile(temp, string(), theconfig, doc)) {
	    QMessageBox::warning(0, "Recoll",
				 tr("Cannot extract document or create "
				    "temporary file"));
	    return;
	}
	istempfile = true;
	rememberTempFile(temp);
	fn = temp->filename();
	url = string("file://") + fn;
    }

    // If using an actual file, check that it exists, and if it is
    // compressed, we may need an uncompressed version
    if (!fn.empty() && theconfig->mimeViewerNeedsUncomp(doc.mimetype)) {
        if (access(fn.c_str(), R_OK) != 0) {
            QMessageBox::warning(0, "Recoll", 
                                 tr("Can't access file: ") + 
                                 QString::fromLocal8Bit(fn.c_str()));
            return;
        }
        TempFile temp;
        if (FileInterner::isCompressed(fn, theconfig)) {
            if (!FileInterner::maybeUncompressToTemp(temp, fn, theconfig,  
                                                     doc)) {
                QMessageBox::warning(0, "Recoll", 
                                     tr("Can't uncompress file: ") + 
                                     QString::fromLocal8Bit(fn.c_str()));
                return;
            }
        }
        if (!temp.isNull()) {
	    rememberTempFile(temp);
            fn = temp->filename();
            url = string("file://") + fn;
        }
    }

    // Get rid of the command name. lcmd is now argv[1...n]
    lcmd.pop_front();

    // Substitute %xx inside arguments
    string efftime;
    if (!doc.dmtime.empty() || !doc.fmtime.empty()) {
        efftime = doc.dmtime.empty() ? doc.fmtime : doc.dmtime;
    } else {
        efftime = "0";
    }
    // Try to keep the letters used more or less consistent with the reslist
    // paragraph format.
    map<string, string> subs;
    subs["D"] = efftime;
    subs["f"] = fn;
    subs["F"] = orgfn;
    subs["i"] = doc.ipath;
    subs["M"] = doc.mimetype;
    subs["U"] = url;
    subs["u"] = url;
    // Let %(xx) access all metadata.
    for (map<string,string>::const_iterator it = doc.meta.begin();
         it != doc.meta.end(); it++) {
        subs[it->first] = it->second;
    }
    string ncmd;
    for (list<string>::iterator it = lcmd.begin(); 
         it != lcmd.end(); it++) {
        pcSubst(*it, ncmd, subs);
        LOGDEB(("%s->%s\n", it->c_str(), ncmd.c_str()));
        *it = ncmd;
    }

    // Also substitute inside the unsplitted command line and display
    // in status bar
    pcSubst(cmd, ncmd, subs);
    ncmd += " &";
    QStatusBar *stb = statusBar();
    if (stb) {
	string fcharset = theconfig->getDefCharset(true);
	string prcmd;
	transcode(ncmd, prcmd, fcharset, "UTF-8");
	QString msg = tr("Executing: [") + 
	    QString::fromUtf8(prcmd.c_str()) + "]";
	stb->showMessage(msg, 5000);
    }

    if (!istempfile)
	historyEnterDoc(g_dynconf, doc.meta[Rcl::Doc::keyudi]);

    // We keep pushing back and never deleting. This can't be good...
    ExecCmd *ecmd = new ExecCmd;
    m_viewers.push_back(ecmd);
    ecmd->startExec(cmdpath, lcmd, false, false);
}

void RclMain::startManual()
{
    startManual(string());
}

void RclMain::startManual(const string& index)
{
    Rcl::Doc doc;
    doc.url = "file://";
    doc.url = path_cat(doc.url, theconfig->getDatadir());
    doc.url = path_cat(doc.url, "doc");
    doc.url = path_cat(doc.url, "usermanual.html");
    LOGDEB(("RclMain::startManual: help index is %s\n", 
	    index.empty()?"(null)":index.c_str()));
    if (!index.empty()) {
	doc.url += "#";
	doc.url += index;
    }
    doc.mimetype = "text/html";
    startNativeViewer(doc);
}

// Search for document 'like' the selected one. We ask rcldb/xapian to find
// significant terms, and add them to the simple search entry.
void RclMain::docExpand(Rcl::Doc doc)
{
    LOGDEB(("RclMain::docExpand()\n"));
    if (!rcldb)
	return;
    list<string> terms;

    terms = m_source->expand(doc);
    if (terms.empty()) {
	LOGDEB(("RclMain::docExpand: no terms\n"));
	return;
    }
    // Do we keep the original query. I think we'd better not.
    // rcldb->expand is set to keep the original query terms instead.
    QString text;// = sSearch->queryText->currentText();
    for (list<string>::iterator it = terms.begin(); it != terms.end(); it++) {
	text += QString::fromLatin1(" \"") +
	    QString::fromUtf8((*it).c_str()) + QString::fromLatin1("\"");
    }
    // We need to insert item here, its not auto-done like when the user types
    // CR
    sSearch->queryText->setEditText(text);
    sSearch->setAnyTermMode();
    sSearch->startSimpleSearch();
}

void RclMain::showDocHistory()
{
    LOGDEB(("RclMain::showDocHistory\n"));
    emit searchReset();
    m_source = RefCntr<DocSequence>();
    curPreview = 0;

    string reason;
    if (!maybeOpenDb(reason)) {
	QMessageBox::critical(0, "Recoll", QString(reason.c_str()));
	return;
    }
    // Construct a bogus SearchData structure
    RefCntr<Rcl::SearchData>searchdata = 
	RefCntr<Rcl::SearchData>(new Rcl::SearchData(Rcl::SCLT_AND));
    searchdata->setDescription((const char *)tr("History data").toUtf8());


    // If you change the title, also change it in eraseDocHistory()
    DocSequenceHistory *src = 
	new DocSequenceHistory(rcldb, g_dynconf, 
			       string(tr("Document history").toUtf8()));
    src->setDescription((const char *)tr("History data").toUtf8());
    DocSource *source = new DocSource(theconfig, RefCntr<DocSequence>(src));
    m_source = RefCntr<DocSequence>(source);
    m_source->setSortSpec(m_sortspec);
    m_source->setFiltSpec(m_filtspec);
    emit docSourceChanged(m_source);
    emit sortDataChanged(m_sortspec);
    initiateQuery();
}

// Erase all memory of documents viewed
void RclMain::eraseDocHistory()
{
    // Clear file storage
    if (g_dynconf)
	g_dynconf->eraseAll(docHistSubKey);
    // Clear possibly displayed history
    if (reslist->displayingHistory()) {
	showDocHistory();
    }
}

void RclMain::eraseSearchHistory()
{
    prefs.ssearchHistory.clear();
    sSearch->queryText->clear();
}

// Called when the uiprefs dialog is ok'd
void RclMain::setUIPrefs()
{
    if (!uiprefs)
	return;
    LOGDEB(("Recollmain::setUIPrefs\n"));
    reslist->setFont();
}

void RclMain::enableNextPage(bool yesno)
{
    if (!displayingTable)
	nextPageAction->setEnabled(yesno);
}

void RclMain::enablePrevPage(bool yesno)
{
    if (!displayingTable) {
	prevPageAction->setEnabled(yesno);
	firstPageAction->setEnabled(yesno);
    }
}

QString RclMain::getQueryDescription()
{
    if (m_source.isNull())
	return "";
    return QString::fromUtf8(m_source->getDescription().c_str());
}

/** Show detailed expansion of a query */
void RclMain::showQueryDetails()
{
    if (m_source.isNull())
	return;
    string oq = breakIntoLines(m_source->getDescription(), 100, 50);
    QString str;
    QString desc = tr("Result count (est.)") + ": " + 
	str.setNum(m_source->getResCnt()) + "<br>";
    desc += tr("Query details") + ": " + QString::fromUtf8(oq.c_str());
    QMessageBox::information(this, tr("Query details"), desc);
}

// User pressed a category button: set filter params in reslist
void RclMain::catgFilter(int id)
{
    LOGDEB(("RclMain::catgFilter: id %d\n"));
    if (id < 0 || id >= int(m_catgbutvec.size()))
	return; 

    m_filtspec.reset();

    if (id != 0)  {
	string catg = m_catgbutvec[id];
	string frag;
	theconfig->getGuiFilter(catg, frag);
	m_filtspec.orCrit(DocSeqFiltSpec::DSFS_QLANG, frag);
    }
    LOGDEB(("RclMain::catgFilter: calling setFiltSpec\n"));
    if (m_source.isNotNull())
	m_source->setFiltSpec(m_filtspec);
    initiateQuery();
}

void RclMain::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

bool RclMain::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)  {
        LOGDEB2(("RclMain::eventFilter: keypress\n"));
	// We used to map shift-home to reslist "goto first page"
	// here but we now use shift-pageUp because shift-home is
	// useful to select all inside the search entry
    } else if (event->type() == QEvent::Show)  {
	LOGDEB2(("RclMain::eventFilter: Show\n"));
	// move the focus to the search entry on show
	sSearch->queryText->setFocus();
    }
    return false;
}
