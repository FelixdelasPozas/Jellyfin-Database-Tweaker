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
#include <set>
#include <map>

/** \struct ProcessConfiguration
 * \brief Contains the options of the processing thread.
 *
 */
struct ProcessConfiguration
{
    bool processPlaylistImages;    /** true to compute blurhash and insert images in metadata of playlists, false otherwise. */
    bool processPlaylistTracklist; /** true to add tracklist to playlists if empty. */
    bool processTracksArtists;     /** true to add artist and album metadata to items. */
    bool processTracksNumbers;     /** true to add item index in tracks entities. */
    bool processAlbums;            /** true to enter artist, album and image metadata in Album entries. */
    QString imageName;

    ProcessConfiguration()
    : processPlaylistImages{true}
    , processPlaylistTracklist{true}
    , processTracksArtists{true}
    , processTracksNumbers{true}
    , processAlbums{true}
    {};
};

/** \struct PlaylistOperationData
 * \brief Contains the necesary data to modify playlist/albums and tracks images and artists metadata.
 *  Also used for Album operations as the only thing that changes is the path.
 *
 */
struct PlaylistImageOperationData
{
    std::filesystem::path path;      /** path of playlist. */
    std::string           imageData; /** image data, blurhash etc.. */
    std::string           artist;    /** playlist artist name. */
    std::string           album;     /** playlist album title. */
};

/** \struct TrackOperationData
 * \brief Contains the necessary data to set the track number into a music track.
 *
 */
struct TrackNumberOperationData
{
    std::filesystem::path path;     /** path of track */
    unsigned int          trackNum; /** track sequential number (takes into consideration multiple discs. */
};

/** \struct PlaylistTracksOperationData
 * \brief Contains the necessary data to set the tracklist of a playlist.
 *
 */
struct PlaylistTracksOperationData
{
    std::filesystem::path path;             /** path of the playlist. */
    std::set<std::filesystem::path> tracks; /** ordered mp3 tracks */
    std::vector<std::string> track_ids;     /** ordered track ids in the database. */
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

    /** \brief Returns if the database has been modified.
     *
     */
    bool hasModifiedDB() const
    { return m_dbModified; }

  signals:
    void progress(int);
    void message(const QString &);

  protected:
    virtual void run();

  private:
    /** \brief Counts the number of operations to perform in the database.
     *
     */
    void countOperations();

    /** \brief Performs a count operation in the database with the specified WHERE statement.
     * \param[in] where_sql SQL statement starting with WHERE.
     *
     */
    unsigned long countSQLiteOperation(const std::string &where_sql);

    /** \brief Generate Playlist images operations data
     *
     */
    std::vector<PlaylistImageOperationData> generatePlaylistImageOperations();

    /** \brief Generate Tracks operations data.
     *
     */
    std::vector<TrackNumberOperationData> generateTracksNumberOperationData();

    /** \brief Generate Albums operations data.
     * \param[in] playlistOps Playlist images metadata operations to avoid recomputing the same data.
     *
     */
    std::vector<PlaylistImageOperationData> generateAlbumsOperationsData(const std::vector<PlaylistImageOperationData> &playlistOps);

    /** \brief Generata Playlist tracks operations data.
     *
     */
    std::vector<PlaylistTracksOperationData> generatePlaylistTracksOperations();

    /** \brief Performs the playlist image update operations.
     * \param[in] operations List of playlist data operations to update.
     *
     */
    void updatePlaylistImages(const std::vector<PlaylistImageOperationData> &operations);

    /** \brief Performs the tracks numbers operations.
     * \param[in] operations List of tracks data operations to update.
     *
     */
    void updateTrackNumbers(const std::vector<TrackNumberOperationData> &operations);

    /** \brief Performs the album metadata (artist/album name) operations.
     * \param[in] operations List of playlist data operations to update.
     *
     */
    void updateAlbumOperations(const std::vector<PlaylistImageOperationData> & operations);

    /** \brief Performs the playlist tracklist update operations.
     * \param[in] operations List of playlist tracks data operations to update.
     *
     */
    void updatePlaylistTracks(const std::vector<PlaylistTracksOperationData> & operations);

    /** \brief Helper method to check for SQLite execution errors and clean up
     * a little the code. Returns true on success and false on fail (code != expected).
     * \param[in] code Result code of an SQLite operation.
     * \param[in] expectedCode Expected code of the operation.
     * \param[in] line Source code line of the check.
     *
     */
    bool checkSQLiteError(int code, int expectedCode, int line);

    /** \brief Helper method to check the progress value and sends a progress signal.
     *
     */
    void checkProgress(const unsigned long opNumber);

    /** \brief Helper method that parses the given text and returns the artist and album
     * text as strings. In the pair the first is artist, second is album.
     * \param[in] text Text string of the folder containing the audio files.
     *
     */
    std::pair<std::string, std::string> artistAndAlbumMetadata(const std::wstring &text) const;

    /** \brief Helper method to read the image file in the given path and returns the computed
     * blurhash string.
     * \param[in] path Folder contaning the audio and image files.
     *
     */
    std::string albumBlurhash(const std::filesystem::path &path);

    sqlite3             *m_sql3Handle; /** SQLite db handle */
    ProcessConfiguration m_config;     /** process parameters. */
    QString              m_error;      /** error message or empty if none. */
    bool                 m_abort;      /** true to stop the process. */
    bool                 m_dbModified; /** true if database was modified and false otherwise. */
};

#endif // PROCESSTHREAD_H_
