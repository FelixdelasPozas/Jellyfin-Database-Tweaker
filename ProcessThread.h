/*
 File: ProcessThread.h
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

#ifndef PROCESSTHREAD_H_
#define PROCESSTHREAD_H_

// Qt
#include <QThread>

// SQLite3
#include <sqlite3/sqlite3.h>

/** \class ProcessThread
 * \brief Thread to process the database and enter the missing data.
 *
 */
class ProcessThread
: public QThread
{
    Q_OBJECT
  public:
    /** \brief ProcessThread class constructor.
     * \param[in] db SQLite db handle.
     * \param[in] processImages True to enter image data and false otherwise.
     * \param[in] processArtists True to enter artists and albums data and false otherwise.
     *
     */
    explicit ProcessThread(sqlite3* db, bool processImages, bool processArtists, QObject *parent = nullptr);

    /** \brief ProcessThread class virtual destructor.
     *
     */
    virtual ~ProcessThread();

    /** \brief Aborts the thread if running
     *
     */
    void stop()
    { m_abort = true; }

  signals:
    void progress(int);
    void message(QString);

  protected:
    virtual void run();

  private:

    sqlite3 *m_sql3Handle;     /** SQLite db handle */
    bool     m_processImages;  /** true to enter image data, false otherwise. */
    bool     m_processArtists; /** true to enter artists and albums data, false otherwise. */
    bool     m_abort;          /** true to stop the process. */
};

#endif // PROCESSTHREAD_H_
