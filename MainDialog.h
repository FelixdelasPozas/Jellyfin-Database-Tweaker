/*
 File: MainDialog.h
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

#ifndef MAINDIALOG_H_
#define MAINDIALOG_H_

// Qt
#include <QDialog>

// Project
#include <ui_MainDialog.h>

// sqlite3
#include <sqlite3/sqlite3.h>

// C++
#include <memory>

class ProcessThread;

/** \class MainDialog
 * \brief Program dialog
 *
 */
class MainDialog
: public QDialog
, private Ui::MainDialog
{
    Q_OBJECT
  public:
    /** \brief MainDialog class constructor.
     * \param[in] p Raw pointer of parent widget.
     * \param[in] f Dialog flags.
     *
     */
    explicit MainDialog(QWidget *p = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

    /** \brief MainDialog class virtual destructor.
     *
     */
    virtual ~MainDialog();

  public slots:
    /** \brief Prints the given message in the log widget.
     *
     */
    void log(const QString &msg);

  private slots:
    /** \brief Exits the application.
     *
     */
    void onQuitButtonPressed();

    /** \brief Shows the application about dialog.
     *
     */
    void onAboutButtonPressed();

    /** \brief Opens the file dialog to select a database and checks
     * it's validity.
     *
     */
    void onFileButtonPressed();

    /** \brief Starts/Stops the update process.
     *
     */
    void onUpdateButtonPressed();

    /** \brief Updates progress bar.
     *
     */
    void onProgressUpdated(int);

    /** \brief Shows the results and deletes the processing thread.
     *
     */
    void onProcessThreadFinished();

  private:
    /** \brief Helper method to connect UI signals and slots.
     *
     */
    void connectSignals();

    /** \brief Helper method to close the database.
     *
     */
    void closeDatabase();

    /** \brief Helper to catch exceptions when opening a database
     *
     */
    void onFileButtonPressedImplementation();

    /** \brief Helper to display an error message.
     * \param[in] title Window title.
     * \param[in] text Message text.
     *
     */
    void showErrorMessage(const QString title, const QString text);

    sqlite3                       *m_sql3Handle; /** SQLite db handle */
    std::shared_ptr<ProcessThread> m_thread;     /** Thread to process database. */
};

#endif // MAINDIALOG_H_
