/*
 File: ProcessThread.cpp
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
#include <ProcessThread.h>

// Blurhash
#include <blurhash/blurhash.hpp>

// stb_image
#include <blurhash/stb_image.h>

#include <iostream>

//---------------------------------------------------------------
ProcessThread::ProcessThread(sqlite3 *db, bool processImages, bool processArtists, QObject *parent)
: QThread(parent)
, m_sql3Handle{db}
, m_processImages{processImages}
, m_processArtists{processArtists}
, m_abort{false}
{
}

//---------------------------------------------------------------
ProcessThread::~ProcessThread()
{
}

//---------------------------------------------------------------
void ProcessThread::run()
{
  if(m_sql3Handle)
  {
    // WORK
  }

  emit progress(100);
}
