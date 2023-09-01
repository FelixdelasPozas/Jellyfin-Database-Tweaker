/*
 File: AboutDialog.cpp
 Created on: 01/09/2023
 Author: Felix de las Pozas Alvarez

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Project
#include "AboutDialog.h"

// Qt
#include <QtGlobal>
#include <QDateTime>
#include <QApplication>
#include <QLabel>

// SQLite
extern "C" {
#include <sqlite3/sqlite3.h>
}

const QString AboutDialog::VERSION = QString("version 1.0.0");
const QString COPYRIGHT{"Copyright (c) %1 Félix de las Pozas Álvarez"};

//-----------------------------------------------------------------
AboutDialog::AboutDialog(QWidget *parent, Qt::WindowFlags flags)
: QDialog(parent, flags)
{
  setupUi(this);

  setWindowFlags(windowFlags() & ~(Qt::WindowContextHelpButtonHint) & ~(Qt::WindowMaximizeButtonHint) & ~(Qt::WindowMinimizeButtonHint));
  const auto verStr = tr("version");

  auto compilation_date = QString(__DATE__);
  auto compilation_time = QString(" (") + QString(__TIME__) + QString(")");

  m_compilationDate->setText(tr("Compiled on ") + compilation_date + compilation_time);
  m_qtVersion->setText(QString("%1 %2").arg(verStr).arg(qVersion()));
  m_sqliteVersion->setText(QString("%1 %2").arg(verStr).arg(QString::fromLatin1(sqlite3_version)));
  m_copyright->setText(COPYRIGHT.arg(QDateTime::currentDateTime().date().year()));
  m_version->setText(VERSION);
}
