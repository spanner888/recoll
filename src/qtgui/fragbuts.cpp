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

#include <string>
#include <vector>

#include <QtGui/QHBoxLayout>
#include <QtGui/QVBoxLayout>
#include <QtGui/QCheckBox>
#include <QtGui/QRadioButton>
#include <QtGui/QButtonGroup>
#include <QtXml/QXmlDefaultHandler>

#include "fragbuts.h"
#include "pathut.h"
#include "smallut.h"
#include "recoll.h"
#include "debuglog.h"
#include "readfile.h"

using namespace std;

class FragButsParser : public QXmlDefaultHandler {
public:
    FragButsParser(FragButs *_parent, vector<FragButs::ButFrag>& _buttons)
    : parent(_parent), vlw(new QVBoxLayout(parent)), 
      vl(new QVBoxLayout()), buttons(_buttons),
      hl(0), bg(0), radio(false)
    {
    }

    bool startElement(const QString & /* namespaceURI */,
		      const QString & /* localName */,
		      const QString &qName,
		      const QXmlAttributes &attributes);
    bool endElement(const QString & /* namespaceURI */,
		    const QString & /* localName */,
		    const QString &qName);
    bool characters(const QString &str)
    {
	currentText += str;
	return true;
    }

private:
    QWidget *parent;
    QVBoxLayout *vlw;
    QVBoxLayout *vl;
    vector<FragButs::ButFrag>& buttons;

    // Temporary data while parsing.
    QHBoxLayout *hl;
    QButtonGroup *bg;
    QString currentText;
    QString label;
    string frag;
    bool radio;
};

bool FragButsParser::startElement(const QString & /* namespaceURI */,
                                  const QString & /* localName */,
                                  const QString &qName,
                                  const QXmlAttributes &/*attributes*/)
{
    currentText = "";
    if (qName == "buttons") {
        radio = false;
        hl = new QHBoxLayout();
    } else if (qName == "radiobuttons") {
        radio = true;
        bg = new QButtonGroup(parent);
        hl = new QHBoxLayout();
    }
    return true;
}

bool FragButsParser::endElement(const QString & /* namespaceURI */,
                                const QString & /* localName */,
                                const QString &qName)
{
    if (qName == "label") {
        label = currentText;
    } else if (qName == "frag") {
        frag = qs2utf8s(currentText);
    } else if (qName == "fragbut") {
        string slab = qs2utf8s(label);
        trimstring(slab, " \t\n\t");
        label = QString::fromUtf8(slab.c_str());
        QAbstractButton *abut;
        if (radio) {
            QRadioButton *but = new QRadioButton(label, parent);
            bg->addButton(but);
            if (bg->buttons().length() == 1)
                but->setChecked(true);
            abut = but;
        } else {
            QCheckBox *but = new QCheckBox(label, parent);
            abut = but;
        }
        buttons.push_back(FragButs::ButFrag(abut, frag));
        hl->addWidget(abut);
    } else if (qName == "buttons" || qName == "radiobuttons") {
        vl->addLayout(hl);
        hl = 0;
    } else if (qName == "fragbuts") {
        vlw->addLayout(vl);
    }
    return true;
}

FragButs::FragButs(QWidget* parent)
    : QWidget(parent)
{
    string conf = path_cat(theconfig->getConfDir(), "fragbuts.xml");

    string data, reason;
    if (!file_to_string(conf, data, &reason)) {
        LOGERR(("Fragbuts:: can't read [%s]\n", conf.c_str()));
        return;
    }

    FragButsParser parser(this, m_buttons);
    QXmlSimpleReader reader;
    reader.setContentHandler(&parser);
    reader.setErrorHandler(&parser);
    QXmlInputSource xmlInputSource;
    xmlInputSource.setData(QString::fromUtf8(data.c_str()));
    if (!reader.parse(xmlInputSource)) {
        LOGERR(("FragButs:: parse failed for [%s]\n", conf.c_str()));
        return;
    }
    for (vector<ButFrag>::iterator it = m_buttons.begin(); 
         it != m_buttons.end(); it++) {
        connect(it->button, SIGNAL(clicked(bool)), 
                this, SLOT(onButtonClicked(bool)));
    }
}

FragButs::~FragButs()
{
}

void FragButs::onButtonClicked(bool on)
{
    LOGDEB(("FragButs::onButtonClicked: [%d]\n", int(on)));
    emit fragmentsChanged();
}

void FragButs::getfrags(std::vector<std::string>& frags)
{
    for (vector<ButFrag>::iterator it = m_buttons.begin(); 
         it != m_buttons.end(); it++) {
        if (it->button->isChecked() && !it->fragment.empty()) {
            LOGDEB(("FragButs: fragment [%s]\n", it->fragment.c_str()));
            frags.push_back(it->fragment);
        }
    }
}
