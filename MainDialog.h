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
    virtual ~MainDialog()
    {};

  private slots:
    /** \brief Exits the application.
     *
     */
    void onQuitButtonPressed();

    /** \brief Shows the application about dialog.
     *
     */
    void onAboutButtonPressed();

  private:
    /** \brief Helper method to connect UI signals and slots.
     *
     */
    void connectSignals();
};

#endif // MAINDIALOG_H_
