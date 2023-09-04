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

// Qt
#include <QImage>
#include <QFileInfo>
#include <QDateTime>

// stb_image
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <blurhash/stb_image.h>

const std::string TABLE_NAME = "TypedBaseItems";
const std::string PLAYLIST_VALUE = "MediaBrowser.Controller.Playlists.Playlist";
const std::string ALBUM_VALUE = "MediaBrowser.Controller.Entities.Audio.MusicAlbum";
const int BLURHASH_MAXSIZE = 5;
const QString SEPARATOR = " - ";

//---------------------------------------------------------------
ProcessThread::ProcessThread(sqlite3 *db, bool processImages, bool processArtists, bool processAlbums, const QString imageName, QObject *parent)
: QThread(parent)
, m_sql3Handle{db}
, m_processImages{processImages}
, m_processArtists{processArtists}
, m_processAlbums{processAlbums}
, m_imageName{imageName}
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
void ProcessThread::runImplementation()
{
  int progressVal = 0;
  int count = 0;

  auto checkError = [this](int code, int expectedCode, int line)
  {
    if (code != expectedCode)
    {
      m_error = QString("SQLite3 ERROR in line %1. SQLite3 error is: %2.").arg(line-1).arg(QString::fromStdString(sqlite3_errstr(code)));
      std::cerr << m_error.toStdString() << std::endl;
    }
  };

  if(m_sql3Handle)
  {
    emit progress(progressVal);

    // Count the number of playlists to process, to compute progress value.
    unsigned long totalProgress = 0;
    auto count_callback = [](void *data, int, char **rowData, char **)
    {
      auto counter = reinterpret_cast<unsigned long *>(data);
      *counter = atol(rowData[0]);
      return 0;
    };

    std::string sql = std::string("SELECT COUNT(*) FROM ") + TABLE_NAME + " where type='" + PLAYLIST_VALUE + "'";
    auto result = sqlite3_exec(m_sql3Handle, sql.c_str(), count_callback, &totalProgress, nullptr);
    if (result != SQLITE_OK)
    {
      m_error = QString("Unable to count number of playlist elements. SQLite3 error: %1")
                     .arg(QString::fromLatin1(sqlite3_errstr(result)));
      return;
    }

    emit message(QString("Found %1 playlists/albums to update. Generating UPDATE data...").arg(totalProgress));

    totalProgress *= 3; // two operations per playlist, generate data and apply data.

    // GENERATE Process each individually.
    sqlite3_stmt *stmt;
    sql = std::string("SELECT * FROM ") + TABLE_NAME + " where type='" + PLAYLIST_VALUE + "'";
    result = sqlite3_prepare_v2(m_sql3Handle, sql.c_str(), -1, &stmt, nullptr);
    if (result != SQLITE_OK)
    {
      m_error = QString("Unable to make SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
      sqlite3_finalize(stmt);
      return;
    }

    std::vector<OperationData> operations;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW && !m_abort)
    {
      auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

      const std::filesystem::path playlistPath(pathValue);
      if(!std::filesystem::exists(playlistPath))
      {
        emit message(QString("<span style=\" color:#ff0000;\">Playlist path '%1' doesn't exist!</span>").arg(QString::fromStdWString(playlistPath.wstring())));
        continue;
      }
      std::string album, artist, entryData;

      if(m_processImages)
      {
        std::filesystem::path imagePath;
        for (auto const& dir_entry : std::filesystem::directory_iterator{playlistPath.parent_path()})
        {
          if(dir_entry.path().string().find(m_imageName.toStdString()) != std::string::npos)
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
          }
          else if(n != 3)
          {
            emit message(QString("Couldn't decode '%1' to 3 channel rgb.").arg(QString::fromStdString(imagePath.string())));
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
          }
          stbi_image_free(imageData);
        }
      }

      if(m_processArtists)
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

      const int currentProgress = (++count * 100)/totalProgress;
      if(progressVal != currentProgress)
      {
        progressVal = currentProgress;
        emit progress(progressVal);
      }
    }

    if (result != SQLITE_DONE && !m_abort)
    {
      m_error = QString("Unable to finish SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }
    result = sqlite3_finalize(stmt);
    if(result != SQLITE_OK)
    {
      m_error = QString("Unable to finalize SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }

    if(m_abort)
    {
      m_error = "Aborted operation.";
      return;
    }

    emit message("Finished generating data. Updating database.");

    const std::string ARTISTS_PART = m_processArtists ? "Artists = :artist, AlbumArtists=:artist, Album = :album,":"";
    const std::string IMAGES_PART = m_processImages ? "Images = :image":"";
    std::string strStat = std::string("UPDATE ") + TABLE_NAME + " SET " + ARTISTS_PART + " " + IMAGES_PART + " WHERE Path LIKE :path AND MediaType = 'Audio'";
    sqlite3_stmt * statement;
    result = sqlite3_prepare_v3(m_sql3Handle, strStat.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);

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

      const int currentProgress = (++count * 100)/totalProgress;

      if(!std::filesystem::exists(op.path.parent_path())) continue;

      int artistIdx = 0, albumIdx = 0, imageIdx = 0;

      if(m_processArtists)
      {
        artistIdx = sqlite3_bind_parameter_index(statement, ":artist");
        albumIdx = sqlite3_bind_parameter_index(statement, ":album");
      }

      if(m_processImages)
      {
        imageIdx = sqlite3_bind_parameter_index(statement, ":image");
      }

      const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");
      checkError(result, SQLITE_OK, __LINE__);

      if(m_processArtists)
      {
        result = sqlite3_bind_text(statement, artistIdx, op.artist.c_str(), op.artist.length(), SQLITE_TRANSIENT);
        checkError(result, SQLITE_OK, __LINE__);
        result = sqlite3_bind_text(statement, albumIdx, op.album.c_str(), op.album.length(), SQLITE_TRANSIENT);
        checkError(result, SQLITE_OK, __LINE__);
      }

      if(m_processImages)
      {
        result = sqlite3_bind_text(statement, imageIdx, op.imageData.c_str(), op.imageData.length(), SQLITE_TRANSIENT);
        checkError(result, SQLITE_OK, __LINE__);
      }

      result = sqlite3_bind_text(statement, pathIdx, path.c_str(), path.length(), SQLITE_TRANSIENT);
      checkError(result, SQLITE_OK, __LINE__);

      // For debug
      // std::cout << sqlite3_expanded_sql(statement) << std::endl;

      result = sqlite3_step(statement);
      checkError(result, SQLITE_DONE, __LINE__);
      result = sqlite3_clear_bindings( statement );
      checkError(result, SQLITE_OK, __LINE__);
      result = sqlite3_reset( statement );
      checkError(result, SQLITE_OK, __LINE__);

      if(progressVal != currentProgress)
      {
        progressVal = currentProgress;
        emit progress(progressVal);
      }
    }

    result = sqlite3_finalize(statement);
    checkError(result, SQLITE_OK, __LINE__);

    if(m_processAlbums)
    {
      const std::string ARTISTS_PART = m_processArtists ? "Artists = :artist, AlbumArtists=:artist, Album = :album,":"";
      const std::string IMAGES_PART = m_processImages ? "Images = :image":"";
      std::string strStat = std::string("UPDATE ") + TABLE_NAME + " SET " + ARTISTS_PART + " " + IMAGES_PART + " WHERE Path = :path "
          + "AND MediaType IS NULL AND type ='" + ALBUM_VALUE + "'";

      sqlite3_stmt * statement;
      result = sqlite3_prepare_v3(m_sql3Handle, strStat.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &statement, NULL);

      for(auto &op: operations)
      {
        if(m_abort)
        {
          m_error = "Aborted operation.";
          return;
        }

        emit message(QString("Apply update for '%1' album").arg(QString::fromStdWString(op.path.parent_path().stem().wstring())));

        const auto path = op.path.parent_path().string();

        const int currentProgress = (++count * 100)/totalProgress;

        if(!std::filesystem::exists(op.path.parent_path())) continue;

        int artistIdx = 0, albumIdx = 0, imageIdx = 0;

        if(m_processArtists)
        {
          artistIdx = sqlite3_bind_parameter_index(statement, ":artist");
          albumIdx = sqlite3_bind_parameter_index(statement, ":album");
        }

        if(m_processImages)
        {
          imageIdx = sqlite3_bind_parameter_index(statement, ":image");
        }

        const auto pathIdx = sqlite3_bind_parameter_index(statement, ":path");
        checkError(result, SQLITE_OK, __LINE__);

        if(m_processArtists)
        {
          result = sqlite3_bind_text(statement, artistIdx, op.artist.c_str(), op.artist.length(), SQLITE_TRANSIENT);
          checkError(result, SQLITE_OK, __LINE__);
          result = sqlite3_bind_text(statement, albumIdx, op.album.c_str(), op.album.length(), SQLITE_TRANSIENT);
          checkError(result, SQLITE_OK, __LINE__);
        }

        if(m_processImages)
        {
          result = sqlite3_bind_text(statement, imageIdx, op.imageData.c_str(), op.imageData.length(), SQLITE_TRANSIENT);
          checkError(result, SQLITE_OK, __LINE__);
        }

        result = sqlite3_bind_text(statement, pathIdx, path.c_str(), path.length(), SQLITE_TRANSIENT);
        checkError(result, SQLITE_OK, __LINE__);

        // For debug
        //std::cout << sqlite3_expanded_sql(statement) << std::endl;

        result = sqlite3_step(statement);
        checkError(result, SQLITE_DONE, __LINE__);
        result = sqlite3_clear_bindings( statement );
        checkError(result, SQLITE_OK, __LINE__);
        result = sqlite3_reset( statement );
        checkError(result, SQLITE_OK, __LINE__);

        if(progressVal != currentProgress)
        {
          progressVal = currentProgress;
          emit progress(progressVal);
        }
      }
    }

    result = sqlite3_finalize(statement);
    checkError(result, SQLITE_OK, __LINE__);

    emit message("Finished!");
  }

  emit progress(100);
}
