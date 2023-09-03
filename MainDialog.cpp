/*
 File: MainDialog.cpp
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
#include <MainDialog.h>
#include <AboutDialog.h>
#include <ProcessThread.h>

// Qt
#include <QFileDialog>
#include <QMessageBox>

// C++
#include <filesystem>
#include <mutex>

const std::string TABLE_NAME = "TypedBaseItems";

QString currentPath = QDir::currentPath();

std::mutex log_mutex;

//---------------------------------------------------------------
void sqlite3_log_callback(void *ptr, int iErrCode, const char *zMsg)
{
  std::lock_guard<std::mutex> lg(log_mutex);

  auto obj = reinterpret_cast<MainDialog *>(ptr);
  if(obj && iErrCode != SQLITE_OK)
  {
    obj->log(QString("sqlite3 log: ") + QString::fromLatin1(zMsg));
  }
}

//---------------------------------------------------------------
MainDialog::MainDialog(QWidget *p, Qt::WindowFlags f)
: QDialog(p,f)
, m_sql3Handle{nullptr}
, m_thread{nullptr}
{
  setupUi(this);

  connectSignals();

  sqlite3_initialize();
  sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
  sqlite3_config(SQLITE_CONFIG_LOG, sqlite3_log_callback, this);
}

//---------------------------------------------------------------
MainDialog::~MainDialog()
{
  closeDatabase();

  sqlite3_shutdown();
}

//---------------------------------------------------------------
void MainDialog::connectSignals()
{
  connect(m_quitButton, SIGNAL(pressed()), this, SLOT(onQuitButtonPressed()));
  connect(m_aboutButton, SIGNAL(pressed()), this, SLOT(onAboutButtonPressed()));
  connect(m_openDBButton, SIGNAL(pressed()), this, SLOT(onFileButtonPressed()));
  connect(m_updateButton, SIGNAL(pressed()), this, SLOT(onUpdateButtonPressed()));
}

//---------------------------------------------------------------
void MainDialog::onQuitButtonPressed()
{
  QApplication::quit();
}

//---------------------------------------------------------------
void MainDialog::onAboutButtonPressed()
{
  AboutDialog dialog(this);
  dialog.exec();
}

//---------------------------------------------------------------
void MainDialog::onFileButtonPressed()
{
  try
  {
    onFileButtonPressedImplementation();
  }
  catch(const std::exception &e)
  {
    showErrorMessage("Error opening database", QString("Exception: %1.").arg(QString::fromLatin1(e.what())));
    closeDatabase();
  }
  catch(...)
  {
    showErrorMessage("Error opening database", QString("Unknown exception."));
    closeDatabase();
  }
}

//---------------------------------------------------------------
void MainDialog::onUpdateButtonPressed()
{
  auto button = qobject_cast<QPushButton*>(sender());
  if(button)
  {
    if(button->text().compare("Update DB", Qt::CaseSensitive) == 0)
    {
      if(m_thread) return;
      m_thread = std::make_shared<ProcessThread>(m_sql3Handle, m_playlistImages->isChecked(),
                                                 m_artistAndAlbums->isChecked(), m_imageName->text(), this);

      button->setText("Cancel");
      m_quitButton->setEnabled(false);

      connect(m_thread.get(), SIGNAL(progress(int)), this, SLOT(onProgressUpdated(int)));
      connect(m_thread.get(), SIGNAL(finished()), this, SLOT(onProcessThreadFinished()));
      connect(m_thread.get(), SIGNAL(message(const QString &)), m_log, SLOT(append(const QString &)));

      m_thread->start();
    }
    else
    {
      if(!m_thread) return;

      if(m_thread->isRunning()) m_thread->stop();
    }
  }
}

//---------------------------------------------------------------
void MainDialog::closeDatabase()
{
  if(m_sql3Handle)
  {
    const auto dbPath = sqlite3_db_filename(m_sql3Handle, "main");
    const QString dbName = dbPath ? QString::fromLatin1(dbPath) : QString("Unknown name");
    const auto result = sqlite3_close(m_sql3Handle);
    if(result != SQLITE_OK)
    {
      const auto msg = QString("Unable to close database: '%1'. SQLite3 error: %2").arg(dbName)
                         .arg(QString::fromLatin1(sqlite3_errstr(result)));
      showErrorMessage("Error closing database", msg);
    }

    m_sql3Handle = nullptr;

    log(QString("Database '") + dbName + "' closed.");
  }
}

//---------------------------------------------------------------
void MainDialog::onProgressUpdated(int value)
{
  m_progressBar->setValue(value);
}

//---------------------------------------------------------------
void MainDialog::onProcessThreadFinished()
{
  m_thread = nullptr;

  m_updateButton->setText("Update DB");
  m_quitButton->setEnabled(true);
  m_progressBar->setValue(0);
}

//---------------------------------------------------------------
void MainDialog::onFileButtonPressedImplementation()
{
  auto qdbFile = QFileDialog::getOpenFileName(this, "Select Jellyfin database",
                                               currentPath, tr("Jellyfin database (*.db)"),
                                               nullptr, QFileDialog::ReadOnly);
  if(qdbFile.isEmpty()) return;

  std::filesystem::path dbFile(qdbFile.toStdWString());

  if(!std::filesystem::exists(dbFile))
  {
    showErrorMessage("Error opening database", QString("Unable to open file: '%1'").arg(qdbFile));
    return;
  }

  log(QString("Selected database: ") + qdbFile);

  std::filesystem::path backupDb = dbFile.parent_path();
  currentPath = QString::fromStdString(backupDb.string());

  backupDb /= dbFile.stem();
  backupDb += std::string("_processed") + dbFile.extension().string();

  const auto qBackupDb = QString::fromStdWString(backupDb.wstring());
  log(QString("Attempting to copy database"));

  if(std::filesystem::exists(backupDb))
  {
    showErrorMessage("Error making backup", QString("Unable to backup file: '%1' to '%2'. Destination file exists!").arg(qdbFile).arg(qBackupDb));
    return;
  }

  if(!std::filesystem::copy_file(dbFile, backupDb))
  {
    showErrorMessage("Error making backup", QString("Unable to backup file: '%1' to '%2'. Unable to copy.").arg(qdbFile).arg(qBackupDb));
    return;
  }

  log(QString("Database copied to: %1").arg(qBackupDb));

  // Try to open with sqlite to test if db is locked.
  auto result = sqlite3_open(backupDb.string().c_str(), &m_sql3Handle);
  if(result != SQLITE_OK)
  {
    const auto msg = QString("Unable to open database: '%1'. SQLite3 error: %2").arg(qBackupDb)
                         .arg(QString::fromLatin1(sqlite3_errstr(result)));
    showErrorMessage("Error opening database", msg);

    closeDatabase();
    std::filesystem::remove(backupDb);
    return;
  }

  log(QString("Database opened."));

  sqlite3_stmt *stmt;
  result = sqlite3_prepare_v2(m_sql3Handle, "SELECT * FROM sqlite_master where type='table'", -1, &stmt, nullptr);
  if (result != SQLITE_OK)
  {
    const auto msg = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    showErrorMessage("Error opening database", msg);

    closeDatabase();
    std::filesystem::remove(backupDb);
    return;
  }

  bool hasTable = false;
  while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
  {
    auto name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    if(TABLE_NAME.compare(name) == 0)
    {
      hasTable = true;
      result = SQLITE_DONE;
      break;
    }
  }

  if (result != SQLITE_DONE)
  {
    const auto msg = QString("Unable to finish SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    showErrorMessage("Error opening database", msg);
  }
  sqlite3_finalize(stmt);

  if(!hasTable)
  {
    showErrorMessage("Error opening database", QString("Database: '%1' doesn't contain the correct tables.").arg(qBackupDb));

    closeDatabase();
    std::filesystem::remove(backupDb);
    return;
  }

  log(QString("Database contains the correct tables."));

  // Success, modify UI
  m_BDPath->setText(qdbFile);
  m_BDPath->setEnabled(false);
  m_openDBButton->setEnabled(false);
  m_progressBar->setEnabled(true);
  m_metadata->setEnabled(true);
  m_updateButton->setEnabled(true);
}

//---------------------------------------------------------------
void MainDialog::log(const QString &msg)
{
  m_log->append(msg);
}

//---------------------------------------------------------------
void MainDialog::showErrorMessage(const QString title, const QString text)
{
  QMessageBox msgBox;
  msgBox.setWindowIcon(QIcon(":JellyfinDB/jellyfin-sql-tweak.svg"));
  msgBox.setText(text);
  msgBox.setIcon(QMessageBox::Icon::Critical);
  msgBox.setModal(true);
  msgBox.setWindowTitle(title);
  msgBox.exec();
}
