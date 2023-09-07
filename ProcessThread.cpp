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
#include <iostream>
#include <algorithm>
#include <set>

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

// stb_image
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <blurhash/stb_image.h>

const std::string TABLE_NAME = "TypedBaseItems";
const std::string PLAYLIST_VALUE = "MediaBrowser.Controller.Playlists.Playlist";
const std::string ALBUM_VALUE    = "MediaBrowser.Controller.Entities.Audio.MusicAlbum";
const std::string TRACK_VALUE    = "MediaBrowser.Controller.Entities.Audio.Audio";
const std::string ARTIST_VALUE   = "MediaBrowser.Controller.Entities.Audio.MusicArtist";
const int BLURHASH_MAXSIZE = 5;
const QString SEPARATOR = " - ";

// Playlist data represented as BLOB
const std::string EMPTY_PLAYLIST_BLOB = "7b224f776e6572557365724964223a223030303030303030303030303030303030303030303030303030303030303030222c22536861726573223a5b5d2c22506c61796c6973744d6564696154797065223a22417564696f222c224973526f6f74223a66616c73652c224c696e6b65644368696c6472656e223a5b5d2c2249734844223a66616c73652c22497353686f7274637574223a66616c73652c225769647468223a302c22486569676874223a302c224578747261496473223a5b5d2c22446174654c6173745361766564223a22303030312d30312d30315430303a30303a30302e303030303030305a222c2252656d6f7465547261696c657273223a5b5d2c22537570706f72747345787465726e616c5472616e73666572223a66616c73657d";
const std::string EMPTY_PLAYLIST_TEXT = "{\"OwnerUserId\":\"00000000000000000000000000000000\",\"Shares\":[],\"PlaylistMediaType\":\"Audio\",\"IsRoot\":false,\"LinkedChildren\":[],\"IsHD\":false,\"IsShortcut\":false,\"Width\":0,\"Height\":0,\"ExtraIds\":[],\"DateLastSaved\":\"0001-01-01T00:00:00.0000000Z\",\"RemoteTrailers\":[],\"SupportsExternalTransfer\":false}";

unsigned long operationCount = 0;
unsigned long totalOperations = 0;
int progressValue = 0;

//---------------------------------------------------------------
ProcessThread::ProcessThread(sqlite3 *db, const ProcessConfiguration config, QObject *parent)
: QThread(parent)
, m_sql3Handle{db}
, m_config{config}
, m_abort{false}
{
}

//---------------------------------------------------------------
void ProcessThread::run()
{
  try
  {
    runImplementation();
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
    std::cerr << m_error.toStdString() << '\n';
    std::cerr << "Expected " << expectedCode << " got " << code << std::endl;
    return false;
  }
  return true;
};

//---------------------------------------------------------------
void ProcessThread::runImplementation()
{
  if(m_sql3Handle)
  {
    emit progress(progressValue);

    // Count the number of operations.
    countOperations();

    emit message("Generating UPDATE data...");

    // Generate needed data for updates.
    auto playlistOperations = generatePlaylistImageOperations();

    if(m_abort)
    {
      m_error = "Aborted operation.";
      return;
    }

    auto trackOperations = generateTracksNumberOperationData();

    if(m_abort)
    {
      m_error = "Aborted operation.";
      return;
    }

    auto playlistTracksOperations = generatePlaylistTracksOperations();

    emit message("Finished generating data. Updating database.");

    updatePlaylistImages(playlistOperations);

    if(m_abort)
    {
      m_error = "Aborted operation.";
      return;
    }

    updateAlbumOperations(playlistOperations);

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

    if(m_abort)
    {
      m_error = "Aborted operation.";
      return;
    }

    createArtists(playlistOperations);

    emit message("Finished!");
  }

  emit progress(100);
}

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
    emit message(QString("Unable to perform count operation. Where statement is: %1. SQLite3 error: %2")
        .arg(QString::fromStdString(where_sql)).arg(QString::fromLatin1(sqlite3_errstr(result))));
    return 0;
  }

  return count;
}

//---------------------------------------------------------------
unsigned long ProcessThread::countAlbumsToUpdate()
{
  // TODO
  return 0;
}

//---------------------------------------------------------------
std::vector<PlaylistImageOperationData> ProcessThread::generatePlaylistImageOperations()
{
  std::vector<PlaylistImageOperationData> operations;

  sqlite3_stmt *statement;
  auto sql = std::string("SELECT * FROM ") + TABLE_NAME + " where type='" + PLAYLIST_VALUE + "' AND Images IS NULL";
  auto result = sqlite3_prepare_v2(m_sql3Handle, sql.c_str(), -1, &statement, nullptr);
  if(!checkSQLiteError(result,  SQLITE_OK, __LINE__))
  {
    m_error = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    sqlite3_finalize(statement);
    abort();
    return operations;
  }

  while ((result = sqlite3_step(statement)) == SQLITE_ROW && !m_abort)
  {
    auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));

    const std::filesystem::path playlistPath(pathValue);
    if(!std::filesystem::exists(playlistPath))
    {
      emit message(QString("<span style=\" color:#ff0000;\">Playlist path '%1' doesn't exist!</span>").arg(QString::fromStdWString(playlistPath.wstring())));
      continue;
    }
    std::string album, artist, entryData;

    if(m_config.processImages)
    {
      std::filesystem::path imagePath;
      for (auto const& dir_entry : std::filesystem::directory_iterator{playlistPath.parent_path()})
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
          emit message(QString("Unable to load image '%1'.").arg(QString::fromStdString(imagePath.string())));
          continue;
        }
        else if(n != 3)
        {
          emit message(QString("Couldn't decode '%1' to 3 channel rgb.").arg(QString::fromStdString(imagePath.string())));
          stbi_image_free(imageData);
          continue;
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
          // the same results.
          qImage.scaledToHeight(x*32, Qt::SmoothTransformation);
          qImage.toPixelFormat(QImage::Format_RGB888);

          const auto blurHash = blurhash::encode(qImage.bits(), qImage.width(), qImage.height(), x, y);

          // Stack overflow: https://stackoverflow.com/questions/26109330/datetime-equivalent-in-c
          QFileInfo file(QString::fromStdWString(imagePath.wstring()));
          const auto writeTime = (file.lastModified().toMSecsSinceEpoch() * 10000) + 621355968000009999;

          entryData = std::filesystem::canonical(imagePath).string() + "*" + std::to_string(writeTime)
              + "*Primary*" + std::to_string(width) + "*" + std::to_string(height) + "*" + blurHash;

          // For debug
          // std::cout << entryData << std::endl;
          emit message(QString("Processing '%1'").arg(QString::fromStdWString(imagePath.parent_path().stem().wstring())));
          stbi_image_free(imageData);
        }
      }
    }

    if(m_config.processArtists)
    {
      auto parts = QString::fromStdWString(playlistPath.parent_path().stem().wstring()).split(SEPARATOR);
      if(parts.size() > 1)
      {
        artist = parts.takeFirst().toStdString();
        album = parts.join(SEPARATOR).toStdString();
      }
      else
      {
        parts = QString::fromStdWString(playlistPath.stem().wstring()).split(SEPARATOR);
        if(parts.size() > 1)
        {
          artist = parts.takeFirst().toStdString();
          album = parts.join(SEPARATOR).toStdString();
        }
        else
        {
          std::cerr << "Empty metadata! ERROR in metadata: " << playlistPath.stem().string() << std::endl;
          artist = "Unknown";
          album = playlistPath.stem().string();
        }
      }
    }

    operations.emplace_back(playlistPath, entryData, artist, album);

    const int currentProgress = (++operationCount * 100)/totalOperations;
    if(progressValue != currentProgress)
    {
      progressValue = currentProgress;
      emit progress(progressValue);
    }
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

  return operations;
}

//---------------------------------------------------------------
std::vector<TrackNumberOperationData> ProcessThread::generateTracksNumberOperationData()
{
  std::vector<TrackNumberOperationData> operations;

  if(m_config.processTracks)
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

    while ((result = sqlite3_step(statement)) == SQLITE_ROW && !m_abort)
    {
      auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));

      const std::filesystem::path trackPath(pathValue);
      if(!std::filesystem::exists(trackPath))
      {
        emit message(QString("<span style=\" color:#ff0000;\">Track path '%1' doesn't exist!</span>").arg(QString::fromStdWString(trackPath.wstring())));
        continue;
      }

      const auto trackName = QString::fromStdString(trackPath.stem().string());
      const auto parts = trackName.split(" - ");
      if(parts.size() < 2)
      {
        emit message(QString("<span style=\" color:#ff0000;\">Track path '%1' split error!</span>").arg(QString::fromStdWString(trackPath.wstring())));
        continue;
      }

      const auto numberPart = parts.front();
      const auto diskParts = numberPart.split("-");
      int trackNum = 0;
      if(diskParts.size() > 1)
      {
        // Problems... get all mp3 files in the folder and count the real trackNum
        if(diskParts[0].compare("1") == 0)
        {
          trackNum = diskParts[1].toInt();
        }
        else
        {
          std::set<std::filesystem::path> filenames; // ordered by name
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

      const int currentProgress = (++operationCount * 100)/totalOperations;
      if(progressValue != currentProgress)
      {
        progressValue = currentProgress;
        emit progress(progressValue);
      }
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

  while ((result = sqlite3_step(statement)) == SQLITE_ROW && !m_abort)
  {
    auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));
    std::filesystem::path playlistPath{pathValue};
    if(!std::filesystem::exists(playlistPath.parent_path())) continue;

    std::set<std::filesystem::path> filenames; // ordered by name
    for (auto const& dir_entry : std::filesystem::directory_iterator{playlistPath.parent_path()})
      if(dir_entry.path().extension() == L".mp3")
        filenames.insert(dir_entry.path());

    operations.emplace_back(playlistPath, filenames);

    const int currentProgress = (++operationCount * 100)/totalOperations;

    if(progressValue != currentProgress)
    {
      progressValue = currentProgress;
      emit progress(progressValue);
    }
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
    emit message(QString("Fill track information of playlist '%1").arg(QString::fromStdWString(op.path.filename().wstring())));

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
  }

  result = sqlite3_finalize(statement);
  if(result != SQLITE_OK)
  {
    m_error = QString("Unable to finalize SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
  }

  return operations;
}

//---------------------------------------------------------------
void ProcessThread::updatePlaylistImages(const std::vector<PlaylistImageOperationData> &operations)
{
  const std::string ARTISTS_PART = m_config.processArtists ? "Artists = :artist, AlbumArtists=:artist, Album = :album,":"";
  const std::string IMAGES_PART = m_config.processImages ? "Images = :image":"";
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
    emit message(QString("Apply update for '%1' playlist").arg(QString::fromStdWString(op.path.parent_path().stem().wstring())));

    const auto path = op.path.parent_path().string() + "\\%";

    const int currentProgress = (++operationCount * 100)/totalOperations;

    if(!std::filesystem::exists(op.path.parent_path())) continue;

    int artistIdx = 0, albumIdx = 0, imageIdx = 0;

    if(m_config.processArtists)
    {
      artistIdx = sqlite3_bind_parameter_index(statement, ":artist");
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      albumIdx = sqlite3_bind_parameter_index(statement, ":album");
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    if(m_config.processImages)
    {
      imageIdx = sqlite3_bind_parameter_index(statement, ":image");
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");
    checkSQLiteError(result, SQLITE_OK, __LINE__);

    if(m_config.processArtists)
    {
      result = sqlite3_bind_text(statement, artistIdx, op.artist.c_str(), op.artist.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_bind_text(statement, albumIdx, op.album.c_str(), op.album.length(), SQLITE_TRANSIENT);
      checkSQLiteError(result, SQLITE_OK, __LINE__);
    }

    if(m_config.processImages)
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

    if(progressValue != currentProgress)
    {
      progressValue = currentProgress;
      emit progress(progressValue);
    }
  }

  result = sqlite3_finalize(statement);
  checkSQLiteError(result, SQLITE_OK, __LINE__);
}

//---------------------------------------------------------------
void ProcessThread::updateAlbumOperations(const std::vector<PlaylistImageOperationData> &operations)
{
  if(m_config.processAlbums)
  {
    const std::string ARTISTS_PART = m_config.processArtists ? "Artists = :artist, AlbumArtists=:artist, Album = :album,":"";
    const std::string IMAGES_PART = m_config.processImages ? "Images = :image":"";
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

      emit message(QString("Apply update for '%1' album").arg(QString::fromStdWString(op.path.parent_path().stem().wstring())));

      const auto path = op.path.parent_path().string();

      const int currentProgress = (++operationCount * 100)/totalOperations;

      if(!std::filesystem::exists(op.path.parent_path())) continue;

      int artistIdx = 0, albumIdx = 0, imageIdx = 0;

      if(m_config.processArtists)
      {
        artistIdx = sqlite3_bind_parameter_index(statement, ":artist");
        albumIdx = sqlite3_bind_parameter_index(statement, ":album");
      }

      if(m_config.processImages)
      {
        imageIdx = sqlite3_bind_parameter_index(statement, ":image");
      }

      const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      if(m_config.processArtists)
      {
        result = sqlite3_bind_text(statement, artistIdx, op.artist.c_str(), op.artist.length(), SQLITE_TRANSIENT);
        checkSQLiteError(result, SQLITE_OK, __LINE__);
        result = sqlite3_bind_text(statement, albumIdx, op.album.c_str(), op.album.length(), SQLITE_TRANSIENT);
        checkSQLiteError(result, SQLITE_OK, __LINE__);
      }

      if(m_config.processImages)
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

      if(progressValue != currentProgress)
      {
        progressValue = currentProgress;
        emit progress(progressValue);
      }
    }

    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }
}

//---------------------------------------------------------------
void ProcessThread::updateTrackNumbers(const std::vector<TrackNumberOperationData> &operations)
{
  if(m_config.processTracks)
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
      emit message(QString("Apply update for '%1' track, track number is %2.").arg(trackName).arg(op.trackNum));

      // For debug
      //std::cout << sqlite3_expanded_sql(statement) << std::endl;

      result = sqlite3_step(statement);
      checkSQLiteError(result, SQLITE_DONE, __LINE__);
      result = sqlite3_clear_bindings( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);
      result = sqlite3_reset( statement );
      checkSQLiteError(result, SQLITE_OK, __LINE__);

      const int currentProgress = (++operationCount * 100)/totalOperations;
      if(progressValue != currentProgress)
      {
        progressValue = currentProgress;
        emit progress(progressValue);
      }
    }

    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }
}

// TODO
// Falta crear los ARTIST_VALUE

//---------------------------------------------------------------
void ProcessThread::updatePlaylistTracks(const std::vector<PlaylistTracksOperationData> &operations)
{
  if(m_config.processPlaylists)
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
      // std::cout << "Operation: " << op.path.string() << std::endl;
      emit message(QString("Apply update for '%1' playlist tracks list.").arg(QString::fromStdWString(op.path.parent_path().stem().wstring())));

      QJsonParseError parseError;
      QByteArray ba = QByteArray::fromRawData(EMPTY_PLAYLIST_TEXT.c_str(), EMPTY_PLAYLIST_TEXT.length());
      auto jsonDoc = QJsonDocument::fromJson(ba, &parseError);

      if(jsonDoc.isNull())
      {
        emit message(QString("<span style=\" color:#ff0000;\">Playlist tracklist JSON is null! Path is '%1', parse error is %2.</span>")
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

      const int currentProgress = (++operationCount * 100)/totalOperations;

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

      if(progressValue != currentProgress)
      {
        progressValue = currentProgress;
        emit progress(progressValue);
      }
    }

    result = sqlite3_finalize(statement);
    checkSQLiteError(result, SQLITE_OK, __LINE__);
  }
}

//---------------------------------------------------------------
void ProcessThread::createArtists(const std::vector<PlaylistImageOperationData> &operations)
{
}

//---------------------------------------------------------------
void ProcessThread::countOperations()
{
  if(m_config.processImages)
  {
    const auto where_sql = std::string(" where type='") + PLAYLIST_VALUE + "' AND Images IS NULL";
    const auto playlistCount = countSQLiteOperation(where_sql);
    emit message(QString("Found %1 playlists/albums to update.").arg(playlistCount));
    totalOperations += 2*playlistCount; // generate + apply
  }

  if(m_config.processTracks)
  {
    const auto where_sql = std::string(" where type='") + TRACK_VALUE + "' AND IndexNumber IS NULL";
    const auto tracksCount = countSQLiteOperation(where_sql);
    emit message(QString("Found %1 tracks to update.").arg(tracksCount));
    totalOperations += 2*tracksCount; // generate + apply
  }

  if(m_config.processAlbums)
  {
    const auto where_sql = std::string(" where type='") + ALBUM_VALUE + "'";
    const auto albumsCount = countSQLiteOperation(where_sql);
    emit message(QString("Found %1 albums to update.").arg(albumsCount));
    totalOperations += albumsCount; // apply
  }

  if(m_config.processPlaylists)
  {
    const auto where_sql = std::string(" where type='") + PLAYLIST_VALUE + "' AND data=X'" + EMPTY_PLAYLIST_BLOB + "'";
    const auto tracklistsCount = countSQLiteOperation(where_sql);
    emit message(QString("Found %1 playlist tracklists to update.").arg(tracklistsCount));
    totalOperations += 2*tracklistsCount; // generate + apply
  }

  // Don't know how many "Artist" metadata entries are missing.
}
