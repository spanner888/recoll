/* Copyright (C) 2006 J.F.Dockes 
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
#ifndef _PREVIEW_W_H_INCLUDED_
#define _PREVIEW_W_H_INCLUDED_

#include <stdio.h>

#include <qvariant.h>
#include <qwidget.h>
#include <qtextedit.h>
#include <qimage.h>

#include "rcldb.h"
#include "refcntr.h"
#include "plaintorich.h"
#include "rclmain_w.h"

class QTabWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;
class Preview;
class PlainToRichQtPreview;

class PreviewTextEdit : public QTextEdit {
    Q_OBJECT;
public:
    PreviewTextEdit(QWidget* parent, const char* name, Preview *pv);
    virtual ~PreviewTextEdit();
    void moveToAnchor(const QString& name);
    enum DspType {PTE_DSPTXT, PTE_DSPFLDS, PTE_DSPIMG};

public slots:
    virtual void displayFields();
    virtual void displayText();
    virtual void displayImage();
    virtual void print();
    virtual void createPopupMenu(const QPoint& pos);
    friend class Preview;
protected:
    void mouseDoubleClickEvent(QMouseEvent *);

private:
    PlainToRichQtPreview *m_plaintorich;
    Preview *m_preview;
    bool     m_dspflds;
    string m_url; // filename for this tab
    string m_ipath; // Internal doc path inside file
    int    m_docnum;  // Index of doc in db search results.
    // doc out of internfile (previous fields come from the index) with
    // main text erased (for space).
    Rcl::Doc m_fdoc; 
    // Saved rich (or plain actually) text: the textedit seems to
    // sometimes (but not always) return its text stripped of tags, so
    // this is needed (for printing for example)
    QString  m_richtxt;
    Qt::TextFormat m_format;
    // Temporary file name (possibly, if displaying image). The
    // TempFile itself is kept inside main.cpp (because that's where
    // signal cleanup happens), but we use its name to ask for release
    // when the tab is closed.
    string m_tmpfilename;
    QImage m_image;
    DspType m_curdsp;
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
    void emitWordSelect(QString);
    friend class PreviewTextEdit;

public slots:
    virtual void searchTextLine_textChanged(const QString& text);
    virtual void doSearch(const QString& str, bool next, bool reverse,
			  bool wo = false);
    virtual void nextPressed();
    virtual void prevPressed();
    virtual void currentChanged(QWidget *tw);
    virtual void closeCurrentTab();

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
