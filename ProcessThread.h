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


/** \struct ProcessConfiguration
 * \brief Contains the options of the processing thread.
 *
 */
struct ProcessConfiguration
{
    bool processImages;  /** true to compute blurhash and insert images in metadata of playlists, false otherwise. */
    bool processArtists; /** true to add artist and album metadata to items. */
    bool processAlbums;  /** true to enter artist. album and image metadata in Album entries. */
    bool processTracks;  /** true to add item index in tracks entities. */
    QString imageName;

    ProcessConfiguration(): processImages{true}, processArtists{true}, processAlbums{true}, processTracks{true} {};
};

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
     * \param[in] config ProcessConfiguration object with parameter values.
     * \param[in] parent Raw pointer of the parent QObject.
     *
     */
    explicit ProcessThread(sqlite3* db, const ProcessConfiguration config, QObject *parent = nullptr);

    /** \brief ProcessThread class virtual destructor.
     *
     */
    virtual ~ProcessThread()
    {}

    /** \brief Aborts the thread if running
     *
     */
    void abort()
    { m_abort = true; }

    /** \brief Returns the error text or empty if none.
     *
     */
    QString error() const
    { return m_error; }

    /** \brief Returns the abort status.
     *
     */
    bool isAborted() const
    { return m_abort; }

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

    sqlite3             *m_sql3Handle; /** SQLite db handle */
    ProcessConfiguration m_config;     /** process parameters. */
    bool                 m_abort;          /** true to stop the process. */
    QString              m_error;          /** error message or empty if none. */

    struct OperationData
    {
        std::filesystem::path path;
        std::string           imageData;
        std::string           artist;
        std::string           album;
    };
};

#endif // PROCESSTHREAD_H_
