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
#ifndef RCLMAIN_W_H
#define RCLMAIN_W_H

#include <qvariant.h>
#include <qmainwindow.h>
#include <QFileSystemWatcher>

#include "sortseq.h"
#include "preview_w.h"
#include "recoll.h"
#include "advsearch_w.h"
#include "uiprefs_w.h"
#include "rcldb.h"
#include "searchdata.h"
#include "spell_w.h"
#include "refcntr.h"
#include "pathut.h"

class IdxSchedW;
class ExecCmd;
class Preview;
class ResTable;
class CronToolW;
class RTIToolW;

#include "ui_rclmain.h"

namespace confgui {
    class ConfIndexW;
}

using confgui::ConfIndexW;

class RclMain : public QMainWindow, public Ui::RclMainBase
{
    Q_OBJECT

public:
    RclMain(QWidget * parent = 0) 
	: QMainWindow(parent),
	  curPreview(0),
	  asearchform(0),
	  uiprefs(0),
	  indexConfig(0),
	  indexSched(0),
	  cronTool(0),
	  rtiTool(0),
	  spellform(0),
	  periodictimer(0),
	  restable(0),
	  displayingTable(0),
          m_idNoStem(0),
          m_idAllStem(0),
	  m_idxproc(0),
	  m_sortspecnochange(false),
	  m_periodicToggle(0)
    {
	setupUi(this);
	init();
    }
    ~RclMain() {}
    virtual bool eventFilter(QObject *target, QEvent *event);
    QString getQueryDescription();

public slots:
    virtual bool close();
    virtual void fileExit();
    virtual void idxStatus();
    virtual void periodic100();
    virtual void toggleIndexing();
    virtual void startSearch(RefCntr<Rcl::SearchData> sdata);
    virtual void previewClosed(Preview *w);
    virtual void showAdvSearchDialog();
    virtual void showSpellDialog();
    virtual void showAboutDialog();
    virtual void showMissingHelpers();
    virtual void showActiveTypes();
    virtual void startManual();
    virtual void startManual(const string&);
    virtual void showDocHistory();
    virtual void showExtIdxDialog();
    virtual void showUIPrefs();
    virtual void showIndexConfig();
    virtual void execIndexConfig();
    virtual void showCronTool();
    virtual void execCronTool();
    virtual void showRTITool();
    virtual void execRTITool();
    virtual void showIndexSched();
    virtual void execIndexSched();
    virtual void setUIPrefs();
    virtual void enableNextPage(bool);
    virtual void enablePrevPage(bool);
    virtual void docExpand(Rcl::Doc);
    virtual void startPreview(int docnum, Rcl::Doc doc, int keymods);
    virtual void startPreview(Rcl::Doc);
    virtual void startNativeViewer(Rcl::Doc);
    virtual void saveDocToFile(Rcl::Doc);
    virtual void previewNextInTab(Preview *, int sid, int docnum);
    virtual void previewPrevInTab(Preview *, int sid, int docnum);
    virtual void previewExposed(Preview *, int sid, int docnum);
    virtual void resetSearch();
    virtual void eraseDocHistory();
    virtual void eraseSearchHistory();
    virtual void setStemLang(QAction *id);
    virtual void adjustPrefsMenu();
    virtual void catgFilter(int);
    virtual void initDbOpen();
    virtual void toggleFullScreen();
    virtual void focusToSearch();
    virtual void on_actionSortByDateAsc_toggled(bool on);
    virtual void on_actionSortByDateDesc_toggled(bool on);
    virtual void on_actionShowResultsAsTable_toggled(bool on);
    virtual void onSortDataChanged(DocSeqSortSpec);
    virtual void resultCount(int);
    virtual void showQueryDetails();
    virtual void onResultsChanged();

signals:
    void docSourceChanged(RefCntr<DocSequence>);
    void stemLangChanged(const QString& lang);
    void sortDataChanged(DocSeqSortSpec);
    void filtDataChanged(DocSeqFiltSpec);
    void applyFiltSortData();
    void searchReset();

protected:
    virtual void closeEvent( QCloseEvent * );

private:
    Preview        *curPreview;
    AdvSearch      *asearchform;
    UIPrefsDialog  *uiprefs;
    ConfIndexW     *indexConfig;
    IdxSchedW      *indexSched;
    CronToolW      *cronTool;
    RTIToolW       *rtiTool;
    SpellW         *spellform;
    QTimer         *periodictimer;
    ResTable       *restable;
    bool            displayingTable;
    QAction          *m_idNoStem;
    QAction          *m_idAllStem;
    QFileSystemWatcher m_watcher;

    vector<ExecCmd*>  m_viewers;
    ExecCmd          *m_idxproc; // Indexing process
    map<QString, QAction*> m_stemLangToId;
    vector<string>    m_catgbutvec;
    DocSeqFiltSpec    m_filtspec;
    bool              m_sortspecnochange;
    DocSeqSortSpec    m_sortspec;
    RefCntr<DocSequence> m_source;
    int               m_periodicToggle;

    virtual void init();
    virtual void previewPrevOrNextInTab(Preview *, int sid, int docnum, 
					bool next);
    virtual void setStemLang(const QString& lang);
    virtual void onSortCtlChanged();
    virtual void showIndexConfig(bool modal);
    virtual void showIndexSched(bool modal);
    virtual void showCronTool(bool modal);
    virtual void showRTITool(bool modal);
    virtual void updateIdxForDocs(vector<Rcl::Doc>&);
};

#endif // RCLMAIN_W_H
