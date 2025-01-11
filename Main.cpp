/*
 File: Main.cpp
 Created on: 01/9/2023
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
#include <MainDialog.h>

// Qt
#include <QApplication>
#include <QSharedMemory>
#include <QMessageBox>
#include <QIcon>
#include <QDebug>

// C++
#include <iostream>
#include <filesystem>

//-----------------------------------------------------------------
void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  const char symbols[] =
  { 'I', 'E', '!', 'X' };
//  QString output = QString("[%1] %2 (%3:%4 -> %5)").arg( symbols[type] ).arg( msg ).arg(context.file).arg(context.line).arg(context.function);
  QString output = QString("[%1] %2").arg(symbols[type]).arg(msg);
  std::cerr << output.toStdString() << std::endl;
  if (type == QtFatalMsg) abort();
}

//-----------------------------------------------------------------
int main(int argc, char *argv[])
{
  qInstallMessageHandler(myMessageOutput);

  QApplication app(argc, argv);

  // allow only one instance
  QSharedMemory guard;
  guard.setKey("JellyfinDBTweaker");

  if (!guard.create(1))
  {
    QMessageBox msgBox;
    msgBox.setWindowIcon(QIcon(":JellyfinDB/jellyfin-sql-tweak.svg"));
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("Jellyfin Database Tweaker is already running!");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    exit(0);
  }

  std::filesystem::path dbPath;

  if(argc > 1 && QString(argv[1]).compare("--db") == 0)
  {
    auto filePath = std::filesystem::path(argv[2]);
    if(std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
    {
      dbPath = filePath;
    }
  }

  MainDialog dialog(dbPath);
  dialog.show();

  return app.exec();
}

