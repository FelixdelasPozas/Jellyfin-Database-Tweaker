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

// C++
#include <filesystem>
#include <algorithm>
#include <set>
#include <cassert>
// For debug
#include <iostream>

// Qt
#include <QImage>
#include <QFileInfo>
#include <QDateTime>
#include <QString>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QCoreApplication>

// stb_image
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <blurhash/stb_image.h>

// Jellyfin database table and types to modify
const std::string TABLE_NAME = "TypedBaseItems";
const std::string PLAYLIST_VALUE = "MediaBrowser.Controller.Playlists.Playlist";
const std::string ALBUM_VALUE    = "MediaBrowser.Controller.Entities.Audio.MusicAlbum";
const std::string TRACK_VALUE    = "MediaBrowser.Controller.Entities.Audio.Audio";

// Empty Playlist data represented as BLOB and string.
const std::string EMPTY_PLAYLIST_BLOB = "7b224f776e6572557365724964223a223030303030303030303030303030303030303030303030303030303030303030222c22536861726573223a5b5d2c22506c61796c6973744d6564696154797065223a22417564696f222c224973526f6f74223a66616c73652c224c696e6b65644368696c6472656e223a5b5d2c2249734844223a66616c73652c22497353686f7274637574223a66616c73652c225769647468223a302c22486569676874223a302c224578747261496473223a5b5d2c22446174654c6173745361766564223a22303030312d30312d30315430303a30303a30302e303030303030305a222c2252656d6f7465547261696c657273223a5b5d2c22537570706f72747345787465726e616c5472616e73666572223a66616c73657d";
const std::string EMPTY_PLAYLIST_TEXT = "{\"OwnerUserId\":\"00000000000000000000000000000000\",\"Shares\":[],\"PlaylistMediaType\":\"Audio\",\"IsRoot\":false,\"LinkedChildren\":[],\"IsHD\":false,\"IsShortcut\":false,\"Width\":0,\"Height\":0,\"ExtraIds\":[],\"DateLastSaved\":\"0001-01-01T00:00:00.0000000Z\",\"RemoteTrailers\":[],\"SupportsExternalTransfer\":false}";

const int BLURHASH_MAXSIZE = 5;
const QString SEPARATOR = " - ";

// Global progress values.
unsigned long operationCount = 0;
unsigned long totalOperations = 0;
int currentProgress = 0;

//---------------------------------------------------------------
ProcessThread::ProcessThread(sqlite3 *db, const ProcessConfiguration config, QObject *parent)
: QThread(parent)
, m_sql3Handle{db}
, m_config{config}
, m_abort{false}
, m_dbModified{false}
{
  assert(m_sql3Handle);
}

//---------------------------------------------------------------
void ProcessThread::run()
{
  try
  {
    if(m_sql3Handle)
    {
      emit progress(currentProgress);

      // Count the number of operations for the progress bar.
      //
      countOperations();

      if(totalOperations == 0)
      {
        emit message("No update operations to perform.");
        return;
      }

      emit message("Generating UPDATE data...");

      // Generate needed data for updates.
      //
      const auto playlistOperations = generatePlaylistImageOperations();

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      const auto playlistTracksOperations = generatePlaylistTracksOperations();

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      const auto trackOperations = generateTracksNumberOperationData();

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      const auto albumOperations = generateAlbumsOperationsData(playlistOperations);

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      emit message("Finished generating data, updating database. Please wait...");

      // Apply operations.
      //
      m_dbModified = true;
      updatePlaylistImages(playlistOperations);

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      updateAlbumOperations(albumOperations);

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      updateTrackNumbers(trackOperations);

      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      updatePlaylistTracks(playlistTracksOperations);

      emit message("<b>Finished!</b>");
    }

    emit progress(100);
  }
  catch(const std::exception &e)
  {
    m_error = QString("Exception: %1").arg(QString::fromLatin1(e.what()));
  }
  catch(...)
  {
    m_error = QString("Unknown exception");
  }
}

//---------------------------------------------------------------
bool ProcessThread::checkSQLiteError(int code, int expectedCode, int line)
{
  if (code != expectedCode)
  {
    m_error = QString("SQLite3 ERROR in line %1. SQLite3 error is: %2.").arg(line-1).arg(QString::fromStdString(sqlite3_errstr(code)));
    return false;
  }
  return true;
};

//---------------------------------------------------------------
unsigned long ProcessThread::countSQLiteOperation(const std::string &where_sql)
{
  auto count_callback = [](void *data, int, char **rowData, char **)
  {
    auto counter = reinterpret_cast<unsigned long *>(data);
    *counter = atol(rowData[0]);
    return 0;
  };

  unsigned long count = 0;

  const auto sql = std::string("SELECT COUNT(*) FROM ") + TABLE_NAME + where_sql;
  const auto result = sqlite3_exec(m_sql3Handle, sql.c_str(), count_callback, &count, nullptr);
  if (result != SQLITE_OK)
  {
    emit message(QString("Unable to perform count operation. Where statement is: %1. SQLite3 error: %2.")
        .arg(QString::fromStdString(where_sql)).arg(QString::fromLatin1(sqlite3_errstr(result))));
    return 0;
  }

  return count;
}

//---------------------------------------------------------------
std::vector<PlaylistImageOperationData> ProcessThread::generatePlaylistImageOperations()
{
  std::vector<PlaylistImageOperationData> operations;

  if(m_config.processPlaylistImages)
  {
    sqlite3_stmt *statement;
    auto sql = std::string("SELECT * FROM ") + TABLE_NAME + " where type='" + PLAYLIST_VALUE + "' AND (Images IS NULL OR Album IS NULL OR Artists IS NULL)";
    auto result = sqlite3_prepare_v2(m_sql3Handle, sql.c_str(), -1, &statement, nullptr);
    if(!checkSQLiteError(result,  SQLITE_OK, __LINE__))
    {
      m_error = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
      sqlite3_finalize(statement);
      abort();
      return operations;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return operations;
      }

      const auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));

      const std::filesystem::path playlistPath(pathValue);
      if(!std::filesystem::exists(playlistPath))
      {
        emit message(QString("<span style=\" color:#ff0000;\">Playlist path <b>'%1'</b> doesn't exist!</span>").arg(QString::fromStdWString(playlistPath.wstring())));
        continue;
      }

      emit message(QString("Generate metadata information of playlist <b>'%1'</b>.").arg(QString::fromStdWString(playlistPath.filename().wstring())));

      std::string album, artist, entryData;

      entryData = albumBlurhash(playlistPath.parent_path());

      auto metadata = artistAndAlbumMetadata(playlistPath.parent_path().stem().wstring());
      if(metadata == std::pair<std::string, std::string>())
      {
        metadata = artistAndAlbumMetadata(playlistPath.stem().wstring());
      }

      artist = metadata.first;
      album = metadata.second;

      if(artist.empty() || album.empty())
      {
        artist = "Unknown";
        album = playlistPath.stem().string();
      }

      operations.emplace_back(playlistPath, entryData, artist, album);

      checkProgress(++operationCount);
    }

    if (result != SQLITE_DONE)
    {
      m_error = QString("Unable to finish step SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }
    result = sqlite3_finalize(statement);
    if(result != SQLITE_OK)
    {
      m_error = QString("Unable to finalize SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }
  }

  return operations;
}

//---------------------------------------------------------------
std::vector<PlaylistImageOperationData> ProcessThread::generateAlbumsOperationsData(const std::vector<PlaylistImageOperationData> &playlistOps)
{
  std::vector<PlaylistImageOperationData> operations;

  if(m_config.processAlbums)
  {
    sqlite3_stmt * statement;
    const auto sql = std::string("SELECT * FROM ") + TABLE_NAME + " WHERE type='" + ALBUM_VALUE + "' AND (Images IS NULL OR Album IS NULL OR Artists IS NULL)";
    auto result = sqlite3_prepare_v2(m_sql3Handle, sql.c_str(), -1, &statement, nullptr);
    if(!checkSQLiteError(result, SQLITE_OK, __LINE__))
    {
      m_error = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
      sqlite3_finalize(statement);
      return operations;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return operations;
      }

      auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));

      const std::filesystem::path albumPath(pathValue);

      emit message(QString("Generate metadata information of album <b>'%1'</b>.").arg(QString::fromStdWString(albumPath.filename().wstring())));

      std::string entryData, artist, album;

      auto sameAs = [&albumPath](const PlaylistImageOperationData &data){ return data.path.parent_path() == albumPath; };
      auto it = std::find_if(playlistOps.cbegin(), playlistOps.cend(), sameAs);
      if(it != playlistOps.cend())
      {
        artist = (*it).artist;
        album = (*it).album;
        entryData = (*it).imageData;
      }
      else
      {
        auto metadata = artistAndAlbumMetadata(albumPath.stem().wstring());
        if(metadata != std::pair<std::string, std::string>())
        {
          artist = metadata.first;
          album = metadata.second;
        }
        else
        {
          artist = "Unknown";
          album = albumPath.stem().string();
        }

        entryData = albumBlurhash(albumPath);
      }

      operations.emplace_back(albumPath, entryData, artist, album);

      checkProgress(++operationCount);
    }

    checkSQLiteError(result, SQLITE_DONE, __LINE__);
    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }

  return operations;
}


//---------------------------------------------------------------
std::vector<TrackNumberOperationData> ProcessThread::generateTracksNumberOperationData()
{
  std::vector<TrackNumberOperationData> operations;

  if(m_config.processTracksNumbers)
  {
    sqlite3_stmt * statement;
    const auto sql = std::string("SELECT * FROM ") + TABLE_NAME + " where type='" + TRACK_VALUE + "' AND IndexNumber IS NULL";
    auto result = sqlite3_prepare_v2(m_sql3Handle, sql.c_str(), -1, &statement, nullptr);
    if(!checkSQLiteError(result, SQLITE_OK, __LINE__))
    {
      m_error = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
      sqlite3_finalize(statement);
      return operations;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return operations;
      }

      auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));

      const std::filesystem::path trackPath(pathValue);
      if(!std::filesystem::exists(trackPath))
      {
        emit message(QString("<span style=\" color:#ff0000;\">Track path <b>'%1'</b> doesn't exist!</span>").arg(QString::fromStdWString(trackPath.wstring())));
        continue;
      }

      const auto trackName = QString::fromStdString(trackPath.stem().string());
      const auto parts = trackName.split(" - ");
      if(parts.size() < 2)
      {
        emit message(QString("<span style=\" color:#ff0000;\">Track path <b>'%1'</b> split error!</span>").arg(QString::fromStdWString(trackPath.wstring())));
        continue;
      }

      const auto numberPart = parts.front();
      const auto diskParts = numberPart.split("-");
      int trackNum = 0;
      if(diskParts.size() > 1)
      {
        // Problems... get all mp3 files in the folder and count the real track number.
        if(diskParts[0].compare("1") == 0)
        {
          trackNum = diskParts[1].toInt();
        }
        else
        {
          std::set<std::filesystem::path> filenames; // ordered by name by default.
          for (auto const& dir_entry : std::filesystem::directory_iterator{trackPath.parent_path()})
            filenames.insert(dir_entry.path());

          int fileCount = 1;
          for(auto sPath: filenames)
          {
            if(sPath.extension() != L".mp3") continue;
            if(sPath == trackPath)
              break;
            ++fileCount;
          }

          trackNum = fileCount;
        }
      }
      else
      {
        trackNum = numberPart.toInt();
      }

      operations.emplace_back(trackPath, trackNum);

      checkProgress(++operationCount);
    }

    checkSQLiteError(result, SQLITE_DONE, __LINE__);
    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }

  return operations;
}

//---------------------------------------------------------------
std::vector<PlaylistTracksOperationData> ProcessThread::generatePlaylistTracksOperations()
{
  std::vector<PlaylistTracksOperationData> operations;

  if(m_config.processPlaylistTracklist)
  {
    sqlite3_stmt *statement;
    auto sql = std::string("SELECT * FROM ") + TABLE_NAME + std::string(" WHERE type='") + PLAYLIST_VALUE + "' AND data=X'" + EMPTY_PLAYLIST_BLOB + "'";
    auto result = sqlite3_prepare_v2(m_sql3Handle, sql.c_str(), -1, &statement, nullptr);
    if(!checkSQLiteError(result,  SQLITE_OK, __LINE__))
    {
      m_error = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
      sqlite3_finalize(statement);
      abort();
      return operations;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return operations;
      }

      auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));
      std::filesystem::path playlistPath{pathValue};
      if(!std::filesystem::exists(playlistPath.parent_path())) continue;

      std::set<std::filesystem::path> filenames; // ordered by name by default
      for (auto const& dir_entry : std::filesystem::directory_iterator{playlistPath.parent_path()})
        if(dir_entry.path().extension() == L".mp3")
          filenames.insert(dir_entry.path());

      operations.emplace_back(playlistPath, filenames);
    }

    if (result != SQLITE_DONE && !m_abort)
    {
      m_error = QString("Unable to finish step SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }
    result = sqlite3_finalize(statement);
    if(result != SQLITE_OK)
    {
      m_error = QString("Unable to finalize SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }

    // Fill missing file ids.
    sql = std::string("SELECT * FROM ") + TABLE_NAME + std::string(" WHERE type='") + TRACK_VALUE + "' AND path=:path";
    result = sqlite3_prepare_v3(m_sql3Handle, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
    const int pathIdx = sqlite3_bind_parameter_index(statement, ":path");
    checkSQLiteError(result, SQLITE_OK, __LINE__);
    for(auto &op: operations)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return operations;
      }

      emit message(QString("Generate track information of playlist <b>'%1'</b>.").arg(QString::fromStdWString(op.path.filename().wstring())));

      for(auto &track: op.tracks)
      {
        result = sqlite3_bind_text(statement, pathIdx, track.string().c_str(), track.string().length(), SQLITE_TRANSIENT);
        checkSQLiteError(result, SQLITE_OK, __LINE__);

        result = sqlite3_step(statement);
        checkSQLiteError(result, SQLITE_ROW, __LINE__);

        auto idValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 46));
        op.track_ids.emplace_back(std::string(idValue));

        // This should return done, as we just get one row.
        result = sqlite3_step(statement);
        checkSQLiteError(result, SQLITE_DONE, __LINE__);

        result = sqlite3_clear_bindings( statement );
        checkSQLiteError(result, SQLITE_OK, __LINE__);
        result = sqlite3_reset( statement );
        checkSQLiteError(result, SQLITE_OK, __LINE__);
      }

      checkProgress(++operationCount);
    }

    result = sqlite3_finalize(statement);
    if(result != SQLITE_OK)
    {
      m_error = QString("Unable to finalize SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }
  }

  return operations;
}

//---------------------------------------------------------------
void ProcessThread::updatePlaylistImages(const std::vector<PlaylistImageOperationData> &operations)
{
  const std::string ARTISTS_PART = m_config.processTracksArtists ? "Artists = :artist, AlbumArtists=:artist, Album = :album,":"";
  const std::string IMAGES_PART = m_config.processPlaylistImages ? "Images = :image":"";
  const std::string sql = std::string("UPDATE ") + TABLE_NAME + " SET " + ARTISTS_PART + " " + IMAGES_PART + " WHERE Path LIKE :path AND MediaType = 'Audio'";

  sqlite3_stmt * statement;
  auto result = sqlite3_prepare_v3(m_sql3Handle, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);

  for(auto &op: operations)
  {
    if(m_abort)
    {
      m_error = "Aborted operation.";
      return;
    }

    // For debug
    // std::cout << "Operation: " << op.path.string() << std::endl;

    emit message(QString("Apply update for <b>'%1'</b> playlist metadata.").arg(QString::fromStdWString(op.path.parent_path().stem().wstring())));

    const auto path = op.path.parent_path().string() + "\\%";

    ++operationCount;

    if(!std::filesystem::exists(op.path.parent_path())) continue;

    int artistIdx = 0, albumIdx = 0, imageIdx = 0;

    if(m_config.processTracksArtists)
    {
      artistIdx = sqlite3_bind_parameter_index(statement, ":artist");
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      albumIdx = sqlite3_bind_parameter_index(statement, ":album");
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    if(m_config.processPlaylistImages)
    {
      imageIdx = sqlite3_bind_parameter_index(statement, ":image");
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");
    checkSQLiteError(result, SQLITE_OK, __LINE__);

    if(m_config.processTracksArtists)
    {
      result = sqlite3_bind_text(statement, artistIdx, op.artist.c_str(), op.artist.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_bind_text(statement, albumIdx, op.album.c_str(), op.album.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    if(m_config.processPlaylistImages)
    {
      result = sqlite3_bind_text(statement, imageIdx, op.imageData.c_str(), op.imageData.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    result = sqlite3_bind_text(statement, pathIdx, path.c_str(), path.length(), SQLITE_TRANSIENT);
    checkSQLiteError(result, SQLITE_OK, __LINE__);

    // For debug
    // std::cout << sqlite3_expanded_sql(statement) << std::endl;

    result = sqlite3_step(statement);
    checkSQLiteError(result, SQLITE_DONE, __LINE__);
    result = sqlite3_clear_bindings( statement );
    checkSQLiteError(result, SQLITE_OK, __LINE__);
    result = sqlite3_reset( statement );
    checkSQLiteError(result, SQLITE_OK, __LINE__);

    checkProgress(operationCount);
  }

  result = sqlite3_finalize(statement);
  checkSQLiteError(result, SQLITE_OK, __LINE__);
}

//---------------------------------------------------------------
void ProcessThread::updateAlbumOperations(const std::vector<PlaylistImageOperationData> &operations)
{
  if(m_config.processAlbums)
  {
    const std::string ARTISTS_PART = m_config.processTracksArtists ? "Artists = :artist, AlbumArtists=:artist, Album = :album,":"";
    const std::string IMAGES_PART = m_config.processPlaylistImages ? "Images = :image":"";
    const std::string sql = std::string("UPDATE ") + TABLE_NAME + " SET " + ARTISTS_PART + " " + IMAGES_PART
                          + " WHERE Path = :path " + "AND MediaType IS NULL AND type ='" + ALBUM_VALUE + "'";

    sqlite3_stmt * statement;
    auto result = sqlite3_prepare_v3(m_sql3Handle, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);

    for(auto &op: operations)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return;
      }

      emit message(QString("Apply update for <b>'%1'</b> album metadata.").arg(QString::fromStdWString(op.path.stem().wstring())));

      const auto path = std::filesystem::canonical(op.path).string();

      ++operationCount;

      if(!std::filesystem::exists(op.path)) continue;

      int artistIdx = 0, albumIdx = 0, imageIdx = 0;

      if(m_config.processTracksArtists)
      {
        artistIdx = sqlite3_bind_parameter_index(statement, ":artist");
        albumIdx = sqlite3_bind_parameter_index(statement, ":album");
      }

      if(m_config.processPlaylistImages)
      {
        imageIdx = sqlite3_bind_parameter_index(statement, ":image");
      }

      const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      if(m_config.processTracksArtists)
      {
        result = sqlite3_bind_text(statement, artistIdx, op.artist.c_str(), op.artist.length(), SQLITE_TRANSIENT);
        checkSQLiteError(result, SQLITE_OK, __LINE__);
        result = sqlite3_bind_text(statement, albumIdx, op.album.c_str(), op.album.length(), SQLITE_TRANSIENT);
        checkSQLiteError(result, SQLITE_OK, __LINE__);
      }

      if(m_config.processPlaylistImages)
      {
        result = sqlite3_bind_text(statement, imageIdx, op.imageData.c_str(), op.imageData.length(), SQLITE_TRANSIENT);
        checkSQLiteError(result, SQLITE_OK, __LINE__);
      }

      result = sqlite3_bind_text(statement, pathIdx, path.c_str(), path.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      // For debug
      //std::cout << sqlite3_expanded_sql(statement) << std::endl;

      result = sqlite3_step(statement);
      checkSQLiteError(result, SQLITE_DONE, __LINE__);
      result = sqlite3_clear_bindings( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_reset( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      checkProgress(operationCount);
    }

    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }
}

//---------------------------------------------------------------
void ProcessThread::updateTrackNumbers(const std::vector<TrackNumberOperationData> &operations)
{
  if(m_config.processTracksNumbers)
  {
    const std::string sql = std::string("UPDATE ") + TABLE_NAME + " SET IndexNumber=:index WHERE Path = :path AND type='"
                          + TRACK_VALUE + "'";

    sqlite3_stmt * statement;
    auto result = sqlite3_prepare_v3(m_sql3Handle, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);
    const auto indexIdx = sqlite3_bind_parameter_index(statement, ":index");
    const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");

    for(auto &op: operations)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        sqlite3_finalize(statement);
        return;
      }

      result = sqlite3_bind_int(statement, indexIdx, op.trackNum);
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_bind_text(statement, pathIdx, op.path.string().c_str(), op.path.string().length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      const auto trackName = QString::fromStdString(op.path.stem().string());
      emit message(QString("Apply update for <b>'%1'</b> track, track number is %2.").arg(trackName).arg(op.trackNum));

      // For debug
      //std::cout << sqlite3_expanded_sql(statement) << std::endl;

      result = sqlite3_step(statement);
      checkSQLiteError(result, SQLITE_DONE, __LINE__);
      result = sqlite3_clear_bindings( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_reset( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      checkProgress(++operationCount);
    }

    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }
}

//---------------------------------------------------------------
void ProcessThread::updatePlaylistTracks(const std::vector<PlaylistTracksOperationData> &operations)
{
  if(m_config.processPlaylistTracklist)
  {
    sqlite3_stmt *statement;
    const std::string sql = std::string("UPDATE ") + TABLE_NAME + " SET data=:data WHERE path=:path AND type='"
        + PLAYLIST_VALUE + "'";

    auto result = sqlite3_prepare_v3(m_sql3Handle, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);

    const int pathIdx = sqlite3_bind_parameter_index(statement, ":path");
    checkSQLiteError(result, SQLITE_OK, __LINE__);
    const int dataIdx = sqlite3_bind_parameter_index(statement, ":data");
    checkSQLiteError(result, SQLITE_OK, __LINE__);

    for(auto &op: operations)
    {
      if(m_abort)
      {
        m_error = "Aborted operation.";
        return;
      }

      // For debug
      emit message(QString("Apply update for <b>'%1'</b> playlist tracks list.").arg(QString::fromStdWString(op.path.parent_path().stem().wstring())));

      QJsonParseError parseError;
      QByteArray ba = QByteArray::fromRawData(EMPTY_PLAYLIST_TEXT.c_str(), EMPTY_PLAYLIST_TEXT.length());
      auto jsonDoc = QJsonDocument::fromJson(ba, &parseError);

      if(jsonDoc.isNull())
      {
        emit message(QString("<span style=\" color:#ff0000;\">Playlist tracklist JSON is null! Path is <b>'%1'</b>, parse error is %2.</span>")
                .arg(QString::fromStdWString(op.path.wstring())).arg(parseError.errorString()));
        continue;
      }

      int i = 0;
      QJsonArray trackList;
      for(const auto &trackPath: op.tracks)
      {
        QJsonObject track;
        track["Path"] = QString::fromStdWString(trackPath.filename().wstring());
        track["Type"] = QString("Manual");
        track["ItemId"] = QString::fromStdString(op.track_ids[i++]);

        trackList.push_back(track);
      }
      auto rootJson = jsonDoc.object();
      rootJson["LinkedChildren"] = trackList;
      rootJson["DateLastSaved"] = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ss.zzzzZ");

      jsonDoc = QJsonDocument(rootJson);

      result = sqlite3_bind_text(statement, pathIdx, op.path.string().c_str(), op.path.string().length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      const auto jsonData = jsonDoc.toJson(QJsonDocument::Compact);
      result = sqlite3_bind_blob(statement, dataIdx, jsonData.data(), jsonData.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      // For debug
      // std::cout << sqlite3_expanded_sql(statement) << std::endl;

      result = sqlite3_step(statement);
      checkSQLiteError(result, SQLITE_DONE, __LINE__);
      result = sqlite3_clear_bindings( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_reset( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      checkProgress(++operationCount);
    }

    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }
}

//---------------------------------------------------------------
void ProcessThread::countOperations()
{
  if(m_config.processPlaylistImages)
  {
    const auto where_sql = std::string(" where type='") + PLAYLIST_VALUE + "' AND (Images IS NULL OR Album IS NULL OR Artists IS NULL)";
    const auto playlistCount = countSQLiteOperation(where_sql);
    emit message(QString("Found <b>%1</b> playlists to update image, artists and album metadata.").arg(playlistCount));
    totalOperations += 2*playlistCount; // generate + apply
  }

  if(m_config.processPlaylistTracklist)
  {
    const auto where_sql = std::string(" where type='") + PLAYLIST_VALUE + "' AND data=X'" + EMPTY_PLAYLIST_BLOB + "'";
    const auto tracklistsCount = countSQLiteOperation(where_sql);
    emit message(QString("Found <b>%1</b> playlist to update audio tracks list.").arg(tracklistsCount));
    totalOperations += 2*tracklistsCount; // generate + apply
  }

  if(m_config.processTracksNumbers)
  {
    const auto where_sql = std::string(" where type='") + TRACK_VALUE + "' AND IndexNumber IS NULL";
    const auto tracksCount = countSQLiteOperation(where_sql);
    emit message(QString("Found <b>%1</b> tracks to update track number.").arg(tracksCount));
    totalOperations += 2*tracksCount; // generate + apply
  }

  if(m_config.processAlbums)
  {
    const auto where_sql = std::string(" where type='") + ALBUM_VALUE + "' AND (Images IS NULL OR Album IS NULL OR Artists IS NULL)";
    const auto albumsCount = countSQLiteOperation(where_sql);
    emit message(QString("Found <b>%1</b> albums to update image, artists and album metadata.").arg(albumsCount));
    totalOperations += albumsCount; // apply
  }

}

//---------------------------------------------------------------
void ProcessThread::checkProgress(const unsigned long operationNumber)
{
  const int progressValue = (operationNumber * 100)/totalOperations;
  if(currentProgress != progressValue)
  {
    currentProgress = progressValue;
    emit progress(currentProgress);
  }

  QCoreApplication::processEvents();
}

//---------------------------------------------------------------
std::pair<std::string, std::string> ProcessThread::artistAndAlbumMetadata(const std::wstring &text) const
{
  std::pair<std::string, std::string> result;

  auto parts = QString::fromStdWString(text).split(SEPARATOR);
  if(parts.size() > 1)
  {
    result.first = parts.takeFirst().toStdString();
    result.second = parts.join(SEPARATOR).toStdString();
  }

  return result;
}

//---------------------------------------------------------------
std::string ProcessThread::albumBlurhash(const std::filesystem::path &path)
{
  std::string result;

  std::filesystem::path imagePath;
  for (auto const& dir_entry : std::filesystem::directory_iterator{path})
  {
    if(dir_entry.path().string().find(m_config.imageName.toStdString()) != std::string::npos)
    {
      imagePath = dir_entry.path();
      break;
    }
  }

  if(!imagePath.empty())
  {
    int width, height, n;
    char utfImagePath[1024];
    stbi_convert_wchar_to_utf8(utfImagePath, 1024, imagePath.wstring().c_str());
    unsigned char *imageData = stbi_load(utfImagePath, &width, &height, &n, 3);

    if (!imageData)
    {
      emit message(QString("Unable to load image <b>'%1'</b>.").arg(QString::fromStdString(imagePath.string())));
    }
    else if(n != 3)
    {
      emit message(QString("Couldn't decode <b>'%1'</b> to 3 channel RGB.").arg(QString::fromStdString(imagePath.string())));
      stbi_image_free(imageData);
    }
    else
    {
      QImage qImage(width, height, QImage::Format_RGB888);
      memcpy(qImage.bits(), imageData, width*height*3);

      int x = width;
      int y = height;
      if(width == height) { x = y = BLURHASH_MAXSIZE; }
      else if(width > height) { x /= height; y = BLURHASH_MAXSIZE/x; x = BLURHASH_MAXSIZE; }
      else { y /= width; x = BLURHASH_MAXSIZE/y; y = BLURHASH_MAXSIZE; }

      // Jellyfin scales down images as making a blurhash from the small one has
      // the same results as the blurhash of a big image but takes considerably longer.
      // We do the same.
      qImage.scaledToHeight(x*32, Qt::SmoothTransformation);
      qImage.toPixelFormat(QImage::Format_RGB888);

      const auto blurHash = blurhash::encode(qImage.bits(), qImage.width(), qImage.height(), x, y);

      // Stack overflow: https://stackoverflow.com/questions/26109330/datetime-equivalent-in-c
      // To transform the time in 'ticks'.
      QFileInfo file(QString::fromStdWString(imagePath.wstring()));
      const auto writeTime = (file.lastModified().toMSecsSinceEpoch() * 10000) + 621355968000009999;

      result = std::filesystem::canonical(imagePath).string() + "*" + std::to_string(writeTime)
          + "*Primary*" + std::to_string(width) + "*" + std::to_string(height) + "*" + blurHash;

      // For debug
      // std::cout << result << std::endl;
      stbi_image_free(imageData);
    }
  }

  return result;
}
