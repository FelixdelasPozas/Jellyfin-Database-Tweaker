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

//---------------------------------------------------------------
MainDialog::MainDialog(QWidget *p, Qt::WindowFlags f)
: QDialog(p,f)
{
  setupUi(this);

  connectSignals();
}

//---------------------------------------------------------------
void MainDialog::connectSignals()
{
  connect(m_quitButton, SIGNAL(pressed()), this, SLOT(onQuitButtonPressed()));
  connect(m_aboutButton, SIGNAL(pressed()), this, SLOT(onAboutButtonPressed()));
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
