/* Copyright (C) 2007 J.F.Dockes
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

#include <qglobal.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QFileDialog>
#include <qlayout.h>
#include <qwidget.h>
#include <qlabel.h>
#include <qtimer.h>
#include <qmessagebox.h>
#include <qcheckbox.h>
#include <QListWidget>

#include <list>
using std::list;

#include "confgui.h"
#include "recoll.h"
#include "idxthread.h"
#include "confguiindex.h"
#include "smallut.h"
#include "debuglog.h"
#include "rcldb.h"
#include "conflinkrcl.h"
#include "execmd.h"
#include "rclconfig.h"

namespace confgui {
static const int spacing = 3;
static const int margin = 3;

ConfIndexW::ConfIndexW(QWidget *parent, RclConfig *config)
    : QDialog(parent), m_rclconf(config)
{
    setWindowTitle(QString::fromLocal8Bit(config->getConfDir().c_str()));
    tabWidget = new QTabWidget;
    reloadPanels();

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
				     | QDialogButtonBox::Cancel);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(tabWidget);
    mainLayout->addWidget(buttonBox);
    setLayout(mainLayout);

    resize(QSize(600, 450).expandedTo(minimumSizeHint()));

    connect(buttonBox, SIGNAL(accepted()), this, SLOT(acceptChanges()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(rejectChanges()));
}

void ConfIndexW::acceptChanges()
{
    LOGDEB(("ConfIndexW::acceptChanges()\n"));
    if (!m_conf) {
	LOGERR(("ConfIndexW::acceptChanges: no config\n"));
	return;
    }
    // Let the changes to disk
    if (!m_conf->holdWrites(false)) {
	QMessageBox::critical(0, "Recoll",  
			      tr("Can't write configuration file"));
    }
    // Delete local copy and update the main one from the file
    delete m_conf;
    m_conf = 0;
    m_rclconf->updateMainConfig();
    snapshotConfig();

    if (startIndexingAfterConfig) {
	startIndexingAfterConfig = 0;
	start_indexing(true);
    }
    hide();
}

void ConfIndexW::rejectChanges()
{
    LOGDEB(("ConfIndexW::rejectChanges()\n"));
    // Discard local changes.
    delete m_conf;
    m_conf = 0;
    hide();
}

void ConfIndexW::reloadPanels()
{
    if ((m_conf = m_rclconf->cloneMainConfig()) == 0) 
	return;
    m_conf->holdWrites(true);
    tabWidget->clear();
    m_widgets.clear();

    QWidget *w = new ConfTopPanelW(this, m_conf);
    m_widgets.push_back(w);
    tabWidget->addTab(w, QObject::tr("Global parameters"));
	
    w = new ConfSubPanelW(this, m_conf);
    m_widgets.push_back(w);
    tabWidget->addTab(w, QObject::tr("Local parameters"));

    w = new ConfBeaglePanelW(this, m_conf);
    m_widgets.push_back(w);
    tabWidget->addTab(w, QObject::tr("Beagle web history"));
}

ConfBeaglePanelW::ConfBeaglePanelW(QWidget *parent, ConfNull *config)
    : QWidget(parent)
{
    QVBoxLayout *vboxLayout = new QVBoxLayout(this);
    vboxLayout->setSpacing(spacing);
    vboxLayout->setMargin(margin);

    ConfLink lnk1(new ConfLinkRclRep(config, "processbeaglequeue"));
    ConfParamBoolW* cp1 = 
        new ConfParamBoolW(this, lnk1, tr("Steal Beagle indexing queue"),
                           tr("Beagle MUST NOT be running. Enables processing "
                          "the beagle queue to index Firefox web history.<br>"
                          "(you should also install the Firefox Beagle plugin)"
			  ));
    vboxLayout->addWidget(cp1);

    ConfLink lnk2(new ConfLinkRclRep(config, "webcachedir"));
    ConfParamFNW* cp2 = 
        new ConfParamFNW(this, lnk2, tr("Web page store directory name"),
		     tr("The name for a directory where to store the copies "
			"of visited web pages.<br>"
                        "A non-absolute path is taken relative to the "
			"configuration directory."), true);
    cp2->setEnabled(cp1->m_cb->isChecked());
    connect(cp1->m_cb, SIGNAL(toggled(bool)), cp2, SLOT(setEnabled(bool)));
    vboxLayout->addWidget(cp2);

    ConfLink lnk3(new ConfLinkRclRep(config, "webcachemaxmbs"));
    ConfParamIntW *cp3 =
        new ConfParamIntW(this, lnk3, tr("Max. size for the web store (MB)"),
		      tr("Entries will be recycled once the size is reached"),
                          -1, 1000);
    cp3->setEnabled(cp1->m_cb->isChecked());
    connect(cp1->m_cb, SIGNAL(toggled(bool)), cp3, SLOT(setEnabled(bool)));
    vboxLayout->addWidget(cp3);
    vboxLayout->insertStretch(-1);
}

ConfTopPanelW::ConfTopPanelW(QWidget *parent, ConfNull *config)
    : QWidget(parent)
{
    QGridLayout *gl1 = new QGridLayout(this);
    gl1->setSpacing(spacing);
    gl1->setMargin(margin);

    ConfLink lnktopdirs(new ConfLinkRclRep(config, "topdirs"));
    ConfParamDNLW *etopdirs = new 
	ConfParamDNLW(this, lnktopdirs, tr("Top directories"),
		      tr("The list of directories where recursive "
			 "indexing starts. Default: your home."));
    setSzPol(etopdirs, QSizePolicy::Preferred, QSizePolicy::Preferred, 1, 3);
    gl1->addWidget(etopdirs, 0, 0, 1, 2);

    ConfLink lnkskp(new ConfLinkRclRep(config, "skippedPaths"));
    ConfParamSLW *eskp = new 
	ConfParamSLW(this, lnkskp, tr("Skipped paths"),
		     tr("These are names of directories which indexing "
			"will not enter.<br> May contain wildcards. "
			"Must match "
			"the paths seen by the indexer (ie: if topdirs "
			"includes '/home/me' and '/home' is actually a link "
			"to '/usr/home', a correct skippedPath entry "
			"would be '/home/me/tmp*', not '/usr/home/me/tmp*')"));
    eskp->setFsEncoding(true);
    setSzPol(eskp, QSizePolicy::Preferred, QSizePolicy::Preferred, 1, 3);
    gl1->addWidget(eskp, 1, 0, 1, 2);

    list<string> cstemlangs = Rcl::Db::getStemmerNames();
    QStringList stemlangs;
    for (list<string>::const_iterator it = cstemlangs.begin(); 
	 it != cstemlangs.end(); it++) {
	stemlangs.push_back(QString::fromUtf8(it->c_str()));
    }
    ConfLink lnkidxsl(new ConfLinkRclRep(config, "indexstemminglanguages"));
    ConfParamCSLW *eidxsl = new 
	ConfParamCSLW(this, lnkidxsl, tr("Stemming languages"),
		      tr("The languages for which stemming expansion<br>"
			 "dictionaries will be built."), stemlangs);
    setSzPol(eidxsl, QSizePolicy::Preferred, QSizePolicy::Preferred, 1, 1);
    gl1->addWidget(eidxsl, 2, 0, 1, 2);

    ConfLink lnk4(new ConfLinkRclRep(config, "logfilename"));
    ConfParamFNW *e4 = new 
	ConfParamFNW(this, lnk4, tr("Log file name"),
		     tr("The file where the messages will be written.<br>"
			"Use 'stderr' for terminal output"), false);
    gl1->addWidget(e4, 3, 0, 1, 2);

    QWidget *w = 0;

    ConfLink lnk1(new ConfLinkRclRep(config, "loglevel"));
    w = new ConfParamIntW(this, lnk1, tr("Log verbosity level"),
		      tr("This value adjusts the amount of "
			 "messages,<br>from only errors to a "
			 "lot of debugging data."), 0, 6);
    gl1->addWidget(w, 4, 0);

    ConfLink lnkidxflsh(new ConfLinkRclRep(config, "idxflushmb"));
    w = new ConfParamIntW(this, lnkidxflsh, 
                          tr("Index flush megabytes interval"),
                          tr("This value adjust the amount of "
			 "data which is indexed between flushes to disk.<br>"
			 "This helps control the indexer memory usage. "
			 "Default 10MB "), 0, 1000);
    gl1->addWidget(w, 4, 1);

    ConfLink lnkfsocc(new ConfLinkRclRep(config, "maxfsoccuppc"));
    w = new ConfParamIntW(this, lnkfsocc, tr("Max disk occupation (%)"),
		      tr("This is the percentage of disk occupation where "
			 "indexing will fail and stop (to avoid filling up "
			 "your disk).<br>"
			 "0 means no limit (this is the default)."), 0, 100);
    gl1->addWidget(w, 5, 0);

    ConfLink lnkusfc(new ConfLinkRclRep(config, "usesystemfilecommand"));
    w = new ConfParamBoolW(this, lnkusfc, tr("Use system's 'file' command"),
		       tr("Use the system's 'file' command if internal<br>"
			  "mime type identification fails."));
    gl1->addWidget(w, 5, 1);
    
    ConfLink lnknaspl(new ConfLinkRclRep(config, "noaspell"));
    ConfParamBoolW* cpasp =
    new ConfParamBoolW(this, lnknaspl, tr("No aspell usage"),
		       tr("Disables use of aspell to generate spelling "
			  "approximation in the term explorer tool.<br> "
			  "Useful if aspell is absent or does not work. "));
    gl1->addWidget(cpasp, 6, 0);

    ConfLink lnk2(new ConfLinkRclRep(config, "aspellLanguage"));
    ConfParamStrW* cpaspl =
        new ConfParamStrW(this, lnk2, tr("Aspell language"),
		      tr("The language for the aspell dictionary. "
			 "This should look like 'en' or 'fr' ...<br>"
			 "If this value is not set, the NLS environment "
			 "will be used to compute it, which usually works."
			 "To get an idea of what is installed on your system, "
			 "type 'aspell config' and look for .dat files inside "
			 "the 'data-dir' directory. "));
    cpaspl->setEnabled(!cpasp->m_cb->isChecked());
    connect(cpasp->m_cb, SIGNAL(toggled(bool)), cpaspl,SLOT(setDisabled(bool)));
    gl1->addWidget(cpaspl, 6, 1);

    ConfLink lnkdbd(new ConfLinkRclRep(config, "dbdir"));
    ConfParamFNW *edbd = new 
	ConfParamFNW(this, lnkdbd, tr("Database directory name"),
		     tr("The name for a directory where to store the index<br>"
			"A non-absolute path is taken relative to the "
			" configuration directory. The default is 'xapiandb'."
			), true);
    gl1->addWidget(edbd, 7, 0, 1, 2);
}

ConfSubPanelW::ConfSubPanelW(QWidget *parent, ConfNull *config)
    : QWidget(parent), m_config(config)
{
    QVBoxLayout *vboxLayout = new QVBoxLayout(this);
    vboxLayout->setSpacing(spacing);
    vboxLayout->setMargin(margin);

    ConfLink lnksubkeydirs(new ConfLinkNullRep());
    m_subdirs = new 
	ConfParamDNLW(this, lnksubkeydirs, 
		      QObject::tr("<b>Customised subtrees"),
		      QObject::tr("The list of subdirectories in the indexed "
				  "hierarchy <br>where some parameters need "
				  "to be redefined. Default: empty."));
    m_subdirs->getListBox()->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_subdirs->getListBox(), 
	    SIGNAL(currentItemChanged(QListWidgetItem *, QListWidgetItem *)),
	    this, 
	    SLOT(subDirChanged(QListWidgetItem *, QListWidgetItem *)));
    connect(m_subdirs, SIGNAL(entryDeleted(QString)),
	    this, SLOT(subDirDeleted(QString)));
    list<string> allkeydirs = config->getSubKeys(); 
    QStringList qls;
    for (list<string>::const_iterator it = allkeydirs.begin(); 
	 it != allkeydirs.end(); it++) {
	qls.push_back(QString::fromUtf8(it->c_str()));
    }
    m_subdirs->getListBox()->insertItems(0, qls);
    vboxLayout->addWidget(m_subdirs);

    QFrame *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    vboxLayout->addWidget(line2);

    QLabel *explain = new QLabel(this);
    explain->setText(
		     QObject::
		     tr("<i>The parameters that follow are set either at the "
			"top level, if nothing<br>"
			"or an empty line is selected in the listbox above, "
			"or for the selected subdirectory.<br>"
			"You can add or remove directories by clicking "
			"the +/- buttons."));
    vboxLayout->addWidget(explain);


    m_groupbox = new QGroupBox(this);
    setSzPol(m_groupbox, QSizePolicy::Preferred, QSizePolicy::Preferred, 1, 3);

    QGridLayout *gl1 = new QGridLayout(m_groupbox);
    gl1->setSpacing(spacing);
    gl1->setMargin(margin);

    ConfLink lnkskn(new ConfLinkRclRep(config, "skippedNames", &m_sk));
    ConfParamSLW *eskn = new 
	ConfParamSLW(m_groupbox, lnkskn, 
		     QObject::tr("Skipped names"),
		     QObject::tr("These are patterns for file or directory "
				 " names which should not be indexed."));
    eskn->setFsEncoding(true);
    m_widgets.push_back(eskn);
    gl1->addWidget(eskn, 0, 0, 1, 2);

    list<string> args;
    args.push_back("-l");
    ExecCmd ex;
    string icout;
    string cmd = "iconv";
    int status = ex.doexec(cmd, args, 0, &icout);
    if (status) {
	LOGERR(("Can't get list of charsets from 'iconv -l'"));
    }
    icout = neutchars(icout, ",");
    list<string> ccsets;
    stringToStrings(icout, ccsets);
    QStringList charsets;
    charsets.push_back("");
    for (list<string>::const_iterator it = ccsets.begin(); 
	 it != ccsets.end(); it++) {
	charsets.push_back(QString::fromUtf8(it->c_str()));
    }

    ConfLink lnk21(new ConfLinkRclRep(config, "defaultcharset", &m_sk));
    ConfParamCStrW *e21 = new 
	ConfParamCStrW(m_groupbox, lnk21, 
		       QObject::tr("Default character set"),
		       QObject::tr("This is the character set used for reading files "
			  "which do not identify the character set "
			  "internally, for example pure text files.<br>"
			  "The default value is empty, "
			  "and the value from the NLS environnement is used."
			  ), charsets);
    m_widgets.push_back(e21);
    gl1->addWidget(e21, 1, 0, 1, 2);

    ConfLink lnk3(new ConfLinkRclRep(config, "followLinks", &m_sk));
    ConfParamBoolW *e3 = new 
	ConfParamBoolW(m_groupbox, lnk3, 
		        QObject::tr("Follow symbolic links"),
		        QObject::tr("Follow symbolic links while "
			  "indexing. The default is no, "
			  "to avoid duplicate indexing"));
    m_widgets.push_back(e3);
    gl1->addWidget(e3, 2, 0);

    ConfLink lnkafln(new ConfLinkRclRep(config, "indexallfilenames", &m_sk));
    ConfParamBoolW *eafln = new 
	ConfParamBoolW(m_groupbox, lnkafln, 
		       QObject::tr("Index all file names"),
		       QObject::tr("Index the names of files for which the contents "
			  "cannot be identified or processed (no or "
			  "unsupported mime type). Default true"));
    m_widgets.push_back(eafln);
    gl1->addWidget(eafln, 2, 1);

    ConfLink lnkzfmaxkbs(new ConfLinkRclRep(config, "compressedfilemaxkbs"));
    ConfParamIntW *ezfmaxkbs = new 
	ConfParamIntW(m_groupbox, lnkzfmaxkbs, 
		      tr("Max. compressed file size (KB)"),
		      tr("This value sets a threshold beyond which compressed"
			 "files will not be processed. Set to -1 for no "
			 "limit, to 0 for no decompression ever."),
		      -1, 1000000, -1);
    m_widgets.push_back(ezfmaxkbs);
    gl1->addWidget(ezfmaxkbs, 3, 0);

    ConfLink lnktxtmaxmbs(new ConfLinkRclRep(config, "textfilemaxmbs"));
    ConfParamIntW *etxtmaxmbs = new 
	ConfParamIntW(m_groupbox, lnktxtmaxmbs, 
		      tr("Max. text file size (MB)"),
		      tr("This value sets a threshold beyond which text "
			 "files will not be processed. Set to -1 for no "
			 "limit. \nThis is for excluding monster "
                         "log files from the index."),
		      -1, 1000000);
    m_widgets.push_back(etxtmaxmbs);
    gl1->addWidget(etxtmaxmbs, 3, 1);

    ConfLink lnktxtpagekbs(new ConfLinkRclRep(config, "textfilepagekbs"));
    ConfParamIntW *etxtpagekbs = new 
	ConfParamIntW(m_groupbox, lnktxtpagekbs, 
		      tr("Text file page size (KB)"),
		      tr("If this value is set (not equal to -1), text "
                         "files will be split in chunks of this size for "
                         "indexing.\nThis will help searching very big text "
                         " files (ie: log files)."),
		      -1, 1000000);
    m_widgets.push_back(etxtpagekbs);
    gl1->addWidget(etxtpagekbs, 4, 0);

    ConfLink lnkfiltmaxsecs(new ConfLinkRclRep(config, "filtermaxseconds"));
    ConfParamIntW *efiltmaxsecs = new 
	ConfParamIntW(m_groupbox, lnkfiltmaxsecs, 
		      tr("Max. filter exec. time (S)"),
		      tr("External filters working longer than this will be "
                         "aborted. This is for the rare case (ie: postscript) "
                         "where a document could cause a filter to loop"
			 "Set to -1 for no limit.\n"),
		      -1, 10000);
    m_widgets.push_back(efiltmaxsecs);
    gl1->addWidget(efiltmaxsecs, 4, 1);

    vboxLayout->addWidget(m_groupbox);
    subDirChanged(0, 0);
}

void ConfSubPanelW::reloadAll()
{
    for (list<ConfParamW*>::iterator it = m_widgets.begin();
	 it != m_widgets.end(); it++) {
	(*it)->loadValue();
    }
}

void ConfSubPanelW::subDirChanged(QListWidgetItem *current, QListWidgetItem *)
{
    LOGDEB(("ConfSubPanelW::subDirChanged\n"));
	
    if (current == 0 || current->text() == "") {
	m_sk = "";
	m_groupbox->setTitle(tr("Global"));
    } else {
	m_sk = (const char *) current->text().toUtf8();
	m_groupbox->setTitle(current->text());
    }
    LOGDEB(("ConfSubPanelW::subDirChanged: now [%s]\n", m_sk.c_str()));
    reloadAll();
}

void ConfSubPanelW::subDirDeleted(QString sbd)
{
    LOGDEB(("ConfSubPanelW::subDirDeleted(%s)\n", (const char *)sbd.toUtf8()));
    if (sbd == "") {
	// Can't do this, have to reinsert it
	QTimer::singleShot(0, this, SLOT(restoreEmpty()));
	return;
    }
    // Have to delete all entries for submap
    m_config->eraseKey((const char *)sbd.toUtf8());
}

void ConfSubPanelW::restoreEmpty()
{
    LOGDEB(("ConfSubPanelW::restoreEmpty()\n"));
    m_subdirs->getListBox()->insertItem(0, "");
}

} // Namespace confgui
