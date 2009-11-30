#ifndef _PREVIEW_W_H_INCLUDED_
#define _PREVIEW_W_H_INCLUDED_
/* @(#$Id: preview_w.h,v 1.20 2008-12-16 14:20:10 dockes Exp $  (C) 2006 J.F.Dockes */
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

#include <qvariant.h>
#include <qwidget.h>
#include <stdio.h>

#include "rcldb.h"
#include "refcntr.h"
#include "plaintorich.h"

class QTabWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;
class PreviewTextEdit;
class Preview;

#if (QT_VERSION < 0x040000)
#include <qtextedit.h>
#include <private/qrichtext_p.h>
#define QTEXTEDIT QTextEdit
class QPopupMenu;
#define RCLPOPUP QPopupMenu
#else
#include <q3textedit.h>
#include <q3richtext_p.h>
class Q3PopupMenu;
#define RCLPOPUP Q3PopupMenu
#define QTEXTEDIT Q3TextEdit
#endif

// We keep a list of data associated to each tab
class TabData {
public:
    string url; // filename for this tab
    string ipath; // Internal doc path inside file
    int docnum;  // Index of doc in db search results.
    // doc out of internfile (previous fields come from the index) with
    // main text erased (for space).
    Rcl::Doc fdoc; 
    // Saved rich text: the textedit seems to sometimes (but not
    // always) return its text stripped of tags, so this is needed
    // (for printing for example)
    QString  richtxt; 
    TabData() 
	: docnum(-1) 
    {}
};

class PreviewTextEdit : public QTEXTEDIT {
    Q_OBJECT
public:
    PreviewTextEdit(QWidget* parent, const char* name, Preview *pv) 
	: QTEXTEDIT(parent, name), m_preview(pv), m_dspflds(false)
    {}
    void moveToAnchor(const QString& name);
public slots:
    virtual void toggleFields();
    virtual void print();
    friend class Preview;
private:
    virtual RCLPOPUP *createPopupMenu(const QPoint& pos);
    Preview *m_preview;
    TabData  m_data;
    bool     m_dspflds;
};


// Subclass plainToRich to add <termtag>s and anchors to the preview text
class PlainToRichQtPreview : public PlainToRich {
public:
    int lastanchor;
    PlainToRichQtPreview() 
    {
	lastanchor = 0;
    }    
    virtual ~PlainToRichQtPreview() {}
    virtual string header() {
	if (m_inputhtml) {
	    return snull;
	} else {
	    return string("<qt><head><title></title></head><body><pre>");
	}
    }
    virtual string startMatch() {return string("<termtag>");}
    virtual string endMatch() {return string("</termtag>");}
    virtual string termAnchorName(int i) {
	static const char *termAnchorNameBase = "TRM";
	char acname[sizeof(termAnchorNameBase) + 20];
	sprintf(acname, "%s%d", termAnchorNameBase, i);
	if (i > lastanchor)
	    lastanchor = i;
	return string(acname);
    }

    virtual string startAnchor(int i) {
	return string("<a name=\"") + termAnchorName(i) + "\">";
    }
    virtual string endAnchor() {
	return string("</a>");
    }
    virtual string startChunk() { return "<pre>";}
};

class Preview : public QWidget {

    Q_OBJECT

public:

    Preview(int sid, // Search Id
	    const HiliteData& hdata) // Search terms etc. for highlighting
	: QWidget(0), m_searchId(sid), m_hData(hdata)
    {
	init();
    }
    ~Preview(){}

    virtual void closeEvent(QCloseEvent *e );
    virtual bool eventFilter(QObject *target, QEvent *event );
    /** 
     * Arrange for the document to be displayed either by exposing the tab 
     * if already loaded, or by creating a new tab and loading it.
     * @para docnum is used to link back to the result list (to highlight 
     *   paragraph when tab exposed etc.
     */
    virtual bool makeDocCurrent(const Rcl::Doc& idoc, int docnum, 
				bool sametab = false);
    friend class PreviewTextEdit;
public slots:
    virtual void searchTextLine_textChanged(const QString& text);
    virtual void doSearch(const QString& str, bool next, bool reverse,
			  bool wo = false);
    virtual void nextPressed();
    virtual void prevPressed();
    virtual void currentChanged(QWidget *tw);
    virtual void closeCurrentTab();
    virtual void textDoubleClicked(int, int);
    virtual void selecChanged();

signals:
    void previewClosed(Preview *);
    void wordSelect(QString);
    void showNext(Preview *w, int sid, int docnum);
    void showPrev(Preview *w, int sid, int docnum);
    void previewExposed(Preview *w, int sid, int docnum);
    void printCurrentPreviewRequest();

private:
    // Identifier of search in main window. This is used to check that
    // we make sense when requesting the next document when browsing
    // successive search results in a tab.
    int           m_searchId; 

    bool          m_dynSearchActive;
    bool          m_canBeep;
    bool          m_loading;
    QWidget      *m_currentW;
    HiliteData    m_hData;
    bool          m_justCreated; // First tab create is different
    PlainToRichQtPreview m_plaintorich;
    bool          m_haveAnchors; // Search terms are marked in text
    int           m_lastAnchor; // Number of last anchor. Then rewind to 1
    int           m_curAnchor;

    QTabWidget* pvTab;
    QLabel* searchLabel;
    QLineEdit* searchTextLine;
    QPushButton* nextButton;
    QPushButton* prevButton;
    QPushButton* clearPB;
    QCheckBox* matchCheck;

    void init();
    virtual void setCurTabProps(const Rcl::Doc& doc, int docnum);
    virtual PreviewTextEdit *currentEditor();
    virtual PreviewTextEdit *addEditorTab();
    virtual bool loadDocInCurrentTab(const Rcl::Doc& idoc, int dnm);
};

#endif /* _PREVIEW_W_H_INCLUDED_ */
