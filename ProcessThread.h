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

// C++
#include <filesystem>

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
     * \param[in] imageName Name without extension of the image filename to search in each playlist.
     * \param[in] parent Raw pointer of the parent QObject.
     *
     */
    explicit ProcessThread(sqlite3* db, bool processImages,
                           bool processArtists, const QString imageName, QObject *parent = nullptr);

    /** \brief ProcessThread class virtual destructor.
     *
     */
    virtual ~ProcessThread()
    {}

    /** \brief Aborts the thread if running
     *
     */
    void stop()
    { m_abort = true; }

    /** \brief Returns the error text or empty if none.
     *
     */
    QString error() const
    { return m_error; }

  signals:
    void progress(int);
    void message(const QString &);

  protected:
    virtual void run();

  private:
    /** \brief Helper method to catch excepctions from the run method.
     *
     */
    void runImplementation();

    sqlite3 *m_sql3Handle;     /** SQLite db handle */
    bool     m_processImages;  /** true to enter image data, false otherwise. */
    bool     m_processArtists; /** true to enter artists and albums data, false otherwise. */
    QString  m_imageName;      /** name of image filename without extension to look for in playlists path. */
    bool     m_abort;          /** true to stop the process. */
    QString  m_error;          /** error message or empty if none. */

    struct OperationData
    {
        std::filesystem::path path;
        std::string           imageData;
        std::string           artist;
        std::string           album;
    };
};

#endif // PROCESSTHREAD_H_
