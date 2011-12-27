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

#include <stdio.h>

#include <QPushButton>
#include <QMessageBox>
#include <QTimer>

#include "recoll.h"
#include "crontool.h"
#include "ecrontab.h"
#include "smallut.h"

static string marker;

static string idstring(const string& confdir)
{
    // Quote conf dir, there may be spaces and whatelse in there
    return string("RECOLL_CONFDIR=") + escapeShell(confdir);
}

void CronToolW::init()
{
    marker = "RCLCRON_RCLINDEX=";

    enableButton = new QPushButton(tr("Enable"));
    disableButton = new QPushButton(tr("Disable"));
    buttonBox->addButton(enableButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(disableButton, QDialogButtonBox::ActionRole);
    connect(enableButton, SIGNAL(clicked()), this, SLOT(enableCron()));
    connect(disableButton, SIGNAL(clicked()), this, SLOT(disableCron()));

    // Try to read the current values
    if (!theconfig)
	return;

    if (checkCrontabUnmanaged(marker, "recollindex")) {
	QMessageBox::warning(0, "Recoll", 
			     tr("It seems that manually edited entries exist for recollindex, cannot edit crontab"));
	QTimer::singleShot(0, this, SLOT(close()));
    }
    
    string id = idstring(theconfig->getConfDir());
    vector<string> sched;
    if (getCrontabSched(marker, id, sched)) {
	minsLE->setText(QString::fromAscii(sched[0].c_str()));
	hoursLE->setText(QString::fromAscii(sched[1].c_str()));
	daysLE->setText(QString::fromAscii(sched[4].c_str()));
    }
}

void CronToolW::enableCron()
{
    changeCron(true);
}
void CronToolW::disableCron()
{
    changeCron(false);
}

void CronToolW::changeCron(bool enable)
{
    if (!theconfig)
	return;

    string id = idstring(theconfig->getConfDir());
    string cmd("recollindex");

    string reason;

    if (!enable) {
	editCrontab(marker, id, "", "", reason);
	accept();
    } else {
	string mins((const char *)minsLE->text().toAscii());
	string hours((const char *)hoursLE->text().toAscii());
	string days((const char *)daysLE->text().toAscii());
	string sched = mins + " " + hours + "  * * " + days;
	if (editCrontab(marker, id, sched, cmd, reason)) {
	    accept();
	}  else {
	    QMessageBox::warning(0, "Recoll", 
		     tr("Error installing cron entry. Bad syntax in fields ?"));
	}	    
    }
}
