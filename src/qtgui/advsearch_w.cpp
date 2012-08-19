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
#include "advsearch_w.h"

#include <qvariant.h>
#include <qpushbutton.h>
#include <qlabel.h>
#include <qlineedit.h>
#include <qframe.h>
#include <qcheckbox.h>
#include <qevent.h>
#include <qlayout.h>
#include <qtooltip.h>
#include <qwhatsthis.h>
#include <qmessagebox.h>

#include <list>
#include <string>
#include <map>
#include <algorithm>

#ifndef NO_NAMESPACES
using std::list;
using std::string;
using std::map;
using std::unique;
#endif /* NO_NAMESPACES */

#include "recoll.h"
#include "rclconfig.h"
#include "debuglog.h"
#include "searchdata.h"
#include "guiutils.h"
#include "rclhelp.h"

static const int initclausetypes[] = {1, 3, 0, 2, 5};
static const unsigned int iclausescnt = sizeof(initclausetypes) / sizeof(int);
static map<QString,QString> cat_translations;
static map<QString,QString> cat_rtranslations;


void AdvSearch::init()
{
    (void)new HelpClient(this);
    HelpClient::installMap((const char *)objectName().toUtf8(), 
			   "RCL.SEARCH.COMPLEX");

    // signals and slots connections
    connect(delFiltypPB, SIGNAL(clicked()), this, SLOT(delFiltypPB_clicked()));
    connect(searchPB, SIGNAL(clicked()), this, SLOT(runSearch()));
    connect(filterDatesCB, SIGNAL(toggled(bool)), 
	    this, SLOT(filterDatesCB_toggled(bool)));
    connect(filterSizesCB, SIGNAL(toggled(bool)), 
	    this, SLOT(filterSizesCB_toggled(bool)));
    connect(restrictFtCB, SIGNAL(toggled(bool)), 
	    this, SLOT(restrictFtCB_toggled(bool)));
    connect(restrictCtCB, SIGNAL(toggled(bool)), 
	    this, SLOT(restrictCtCB_toggled(bool)));
    connect(dismissPB, SIGNAL(clicked()), this, SLOT(close()));
    connect(browsePB, SIGNAL(clicked()), this, SLOT(browsePB_clicked()));
    connect(addFiltypPB, SIGNAL(clicked()), this, SLOT(addFiltypPB_clicked()));

    connect(delAFiltypPB, SIGNAL(clicked()), 
	    this, SLOT(delAFiltypPB_clicked()));
    connect(addAFiltypPB, SIGNAL(clicked()), 
	    this, SLOT(addAFiltypPB_clicked()));
    connect(saveFileTypesPB, SIGNAL(clicked()), 
	    this, SLOT(saveFileTypes()));
    connect(addClausePB, SIGNAL(clicked()), this, SLOT(addClause()));
    connect(delClausePB, SIGNAL(clicked()), this, SLOT(delClause()));

    conjunctCMB->insertItem(1, tr("All clauses"));
    conjunctCMB->insertItem(2, tr("Any clause"));

    // Create preconfigured clauses
    for (unsigned int i = 0; i < iclausescnt; i++) {
	addClause(initclausetypes[i]);
    }
    // Tune initial state according to last saved
    {
	std::list<SearchClauseW *>::iterator cit = m_clauseWins.begin();
	for (vector<int>::iterator it = prefs.advSearchClauses.begin(); 
	     it != prefs.advSearchClauses.end(); it++) {
	    if (cit != m_clauseWins.end()) {
		(*cit)->tpChange(*it);
		cit++;
	    } else {
		addClause(*it);
	    }
	}
    }
    (*m_clauseWins.begin())->wordsLE->setFocus();

    // Initialize min/max mtime from extrem values in the index
    int minyear, maxyear;
    if (rcldb) {
	rcldb->maxYearSpan(&minyear, &maxyear);
	minDateDTE->setDate(QDate(minyear, 1, 1));
	maxDateDTE->setDate(QDate(maxyear, 12, 31));
    }

    // Initialize lists of accepted and ignored mime types from config
    // and settings
    m_ignTypes = prefs.asearchIgnFilTyps;
    m_ignByCats = prefs.fileTypesByCats;
    restrictCtCB->setEnabled(false);
    restrictCtCB->setChecked(m_ignByCats);
    fillFileTypes();

    subtreeCMB->insertItems(0, prefs.asearchSubdirHist);
    subtreeCMB->setEditText("");

    // The clauseline frame is needed to force designer to accept a
    // vbox to englobe the base clauses grid and 'something else' (the
    // vbox is so that we can then insert SearchClauseWs), but we
    // don't want to see it.
    clauseline->close();

    bool calpop = 0;
    minDateDTE->setCalendarPopup(calpop);
    maxDateDTE->setCalendarPopup(calpop);

    // Translations for known categories
    cat_translations[QString::fromUtf8("texts")] = tr("texts");
    cat_rtranslations[tr("texts")] = QString::fromUtf8("texts"); 

    cat_translations[QString::fromUtf8("spreadsheets")] = tr("spreadsheets");
    cat_rtranslations[tr("spreadsheets")] = QString::fromUtf8("spreadsheets");

    cat_translations[QString::fromUtf8("presentations")] = tr("presentations");
    cat_rtranslations[tr("presentations")] =QString::fromUtf8("presentations");

    cat_translations[QString::fromUtf8("media")] = tr("media");
    cat_rtranslations[tr("media")] = QString::fromUtf8("media"); 

    cat_translations[QString::fromUtf8("messages")] = tr("messages");
    cat_rtranslations[tr("messages")] = QString::fromUtf8("messages"); 

    cat_translations[QString::fromUtf8("other")] = tr("other");
    cat_rtranslations[tr("other")] = QString::fromUtf8("other"); 
}

void AdvSearch::saveCnf()
{
    // Save my state
    prefs.advSearchClauses.clear(); 
    for (std::list<SearchClauseW *>::iterator cit = m_clauseWins.begin();
	 cit != m_clauseWins.end(); cit++) {
	prefs.advSearchClauses.push_back((*cit)->sTpCMB->currentIndex());
    }
}

bool AdvSearch::close()
{
    saveCnf();
    return QWidget::close();
}

void AdvSearch::delAFiltypPB_clicked()
{
    yesFiltypsLB->selectAll();
    delFiltypPB_clicked();
}

void AdvSearch::addClause()
{
    addClause(0);
}

void AdvSearch::addClause(int tp)
{
    SearchClauseW *w = new SearchClauseW(clauseFRM);
    m_clauseWins.push_back(w);
    ((QVBoxLayout *)(clauseFRM->layout()))->addWidget(w);
    w->show();
    w->tpChange(tp);
    if (m_clauseWins.size() > iclausescnt) {
	delClausePB->setEnabled(true);
    } else {
	delClausePB->setEnabled(false);
    }
}

void AdvSearch::delClause()
{
    if (m_clauseWins.size() <= iclausescnt)
	return;
    delete m_clauseWins.back();
    m_clauseWins.pop_back();
    if (m_clauseWins.size() > iclausescnt) {
	delClausePB->setEnabled(true);
    } else {
	delClausePB->setEnabled(false);
    }
}

// Move selected file types from the searched to the ignored box
void AdvSearch::delFiltypPB_clicked()
{
    QList<QListWidgetItem *> items = yesFiltypsLB->selectedItems();
    for (QList<QListWidgetItem *>::iterator it = items.begin(); 
	 it != items.end(); it++) {
	int row = yesFiltypsLB->row(*it);
	QListWidgetItem *item = yesFiltypsLB->takeItem(row);
	noFiltypsLB->insertItem(0, item);
    }
    guiListsToIgnTypes();
}

// Move selected file types from the ignored to the searched box
void AdvSearch::addFiltypPB_clicked()
{
    QList<QListWidgetItem *> items = noFiltypsLB->selectedItems();
    for (QList<QListWidgetItem *>::iterator it = items.begin(); 
	 it != items.end(); it++) {
	int row = noFiltypsLB->row(*it);
	QListWidgetItem *item = noFiltypsLB->takeItem(row);
	yesFiltypsLB->insertItem(0, item);
    }
    guiListsToIgnTypes();
 }

// Compute list of ignored mime type from widget lists
void AdvSearch::guiListsToIgnTypes()
{
    yesFiltypsLB->sortItems();
    noFiltypsLB->sortItems();
    m_ignTypes.clear();
    for (int i = 0; i < noFiltypsLB->count();i++) {
	QListWidgetItem *item = noFiltypsLB->item(i);
	m_ignTypes.append(item->text());
    }
}
void AdvSearch::addAFiltypPB_clicked()
{
    noFiltypsLB->selectAll();
    addFiltypPB_clicked();
}

// Activate file type selection
void AdvSearch::restrictFtCB_toggled(bool on)
{
    restrictCtCB->setEnabled(on);
    yesFiltypsLB->setEnabled(on);
    delFiltypPB->setEnabled(on);
    addFiltypPB->setEnabled(on);
    delAFiltypPB->setEnabled(on);
    addAFiltypPB->setEnabled(on);
    noFiltypsLB->setEnabled(on);
    saveFileTypesPB->setEnabled(on);
}

// Activate file type selection
void AdvSearch::filterSizesCB_toggled(bool on)
{
    minSizeLE->setEnabled(on);
    maxSizeLE->setEnabled(on);
}
// Activate file type selection
void AdvSearch::filterDatesCB_toggled(bool on)
{
    minDateDTE->setEnabled(on);
    maxDateDTE->setEnabled(on);
}

void AdvSearch::restrictCtCB_toggled(bool on)
{
    m_ignByCats = on;
    // Only reset the list if we're enabled. Else this is init from prefs
    if (restrictCtCB->isEnabled())
	m_ignTypes.clear();
    fillFileTypes();
}

void AdvSearch::fillFileTypes()
{
    noFiltypsLB->clear();
    yesFiltypsLB->clear();
    noFiltypsLB->insertItems(0, m_ignTypes); 

    QStringList ql;
    if (m_ignByCats == false) {
	vector<string> types = theconfig->getAllMimeTypes();
	for (vector<string>::iterator it = types.begin(); 
	     it != types.end(); it++) {
	    QString qs = QString::fromUtf8(it->c_str());
	    if (m_ignTypes.indexOf(qs) < 0)
		ql.append(qs);
	}
    } else {
	vector<string> cats;
	theconfig->getMimeCategories(cats);
	for (vector<string>::const_iterator it = cats.begin();
	     it != cats.end(); it++) {
	    map<QString, QString>::const_iterator it1;
	    QString cat;
	    if ((it1 = cat_translations.find(QString::fromUtf8(it->c_str())))
		!= cat_translations.end()) {
		cat = it1->second;
	    } else {
		cat = QString::fromUtf8(it->c_str());
	    } 
	    if (m_ignTypes.indexOf(cat) < 0)
		ql.append(cat);
	}
    }
    yesFiltypsLB->insertItems(0, ql);
}

// Save current set of ignored file types to prefs
void AdvSearch::saveFileTypes()
{
    prefs.asearchIgnFilTyps = m_ignTypes;
    prefs.fileTypesByCats = m_ignByCats;
    rwSettings(true);
}

size_t AdvSearch::stringToSize(QString qsize)
{
    size_t size = size_t(-1);
    qsize.replace(QRegExp("[\\s]+"), "");
    if (!qsize.isEmpty()) {
	string csize((const char*)qsize.toAscii());
	char *cp;
	size = strtoll(csize.c_str(), &cp, 10);
	if (*cp != 0) {
	    switch (*cp) {
	    case 'k': case 'K': size *= 1E3;break;
	    case 'm': case 'M': size *= 1E6;break;
	    case 'g': case 'G': size *= 1E9;break;
	    case 't': case 'T': size *= 1E12;break;
	    default: 
		QMessageBox::warning(0, "Recoll", 
			     tr("Bad multiplier suffix in size filter"));
		size = size_t(-1);
	    }
	}
    }
    return size;
}

using namespace Rcl;
void AdvSearch::runSearch()
{
    RefCntr<SearchData> sdata(new SearchData(conjunctCMB->currentIndex() == 0 ?
					     SCLT_AND : SCLT_OR));
    bool hasclause = false;

    for (list<SearchClauseW*>::iterator it = m_clauseWins.begin();
	 it != m_clauseWins.end(); it++) {
	SearchDataClause *cl;
	if ((cl = (*it)->getClause())) {
	    sdata->addClause(cl);
            hasclause = true;
	}
    }
    if (!hasclause)
        return;

    if (restrictFtCB->isChecked() && noFiltypsLB->count() > 0) {
	for (int i = 0; i < yesFiltypsLB->count(); i++) {
	    if (restrictCtCB->isChecked()) {
		QString qcat = yesFiltypsLB->item(i)->text();
		map<QString,QString>::const_iterator qit;
		string cat;
		if ((qit = cat_rtranslations.find(qcat)) != 
		    cat_rtranslations.end()) {
		    cat = (const char *)qit->second.toUtf8();
		} else {
		    cat = (const char *)qcat.toUtf8();
		}
		vector<string> types;
		theconfig->getMimeCatTypes(cat, types);
		for (vector<string>::const_iterator it = types.begin();
		     it != types.end(); it++) {
		    sdata->addFiletype(*it);
		}
	    } else {
		sdata->addFiletype((const char *)
				   yesFiltypsLB->item(i)->text().toUtf8());
	    }
	}
    }

    if (filterDatesCB->isChecked()) {
	QDate mindate = minDateDTE->date();
	QDate maxdate = maxDateDTE->date();
	DateInterval di;
	di.y1 = mindate.year();
	di.m1 = mindate.month();
	di.d1 = mindate.day();
	di.y2 = maxdate.year();
	di.m2 = maxdate.month();
	di.d2 = maxdate.day();
	sdata->setDateSpan(&di);
    }
    if (filterSizesCB->isChecked()) {
	size_t size = stringToSize(minSizeLE->text());
	sdata->setMinSize(size);
	size = stringToSize(maxSizeLE->text());
	sdata->setMaxSize(size);
    }

    if (!subtreeCMB->currentText().isEmpty()) {
	QString current = subtreeCMB->currentText();
	sdata->addDirSpec((const char*)subtreeCMB->currentText().toUtf8(),
			  direxclCB->isChecked());
	// Keep history clean and sorted. Maybe there would be a
	// simpler way to do this
	list<QString> entries;
	for (int i = 0; i < subtreeCMB->count(); i++) {
	    entries.push_back(subtreeCMB->itemText(i));
	}
	entries.push_back(subtreeCMB->currentText());
	entries.sort();
	entries.unique();
	subtreeCMB->clear();
	for (list<QString>::iterator it = entries.begin(); 
	     it != entries.end(); it++) {
	    subtreeCMB->addItem(*it);
	}
	subtreeCMB->setCurrentIndex(subtreeCMB->findText(current));
	prefs.asearchSubdirHist.clear();
	for (int index = 0; index < subtreeCMB->count(); index++)
	    prefs.asearchSubdirHist.push_back(subtreeCMB->itemText(index));
    }
    saveCnf();
    
    emit startSearch(sdata);
}

void AdvSearch::browsePB_clicked()
{
    QString dir = myGetFileName(true);
    subtreeCMB->setEditText(dir);
}
