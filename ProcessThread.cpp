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
#include <chrono>
#include <fileapi.h>
#include <io.h>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>

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
const int BLURHASH_MAXSIZE = 5;
const QString SEPARATOR = " - ";

//---------------------------------------------------------------
ProcessThread::ProcessThread(sqlite3 *db, bool processImages, bool processArtists, const QString imageName, QObject *parent)
: QThread(parent)
, m_sql3Handle{db}
, m_processImages{processImages}
, m_processArtists{processArtists}
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

    emit message(QString("Found %1 playlists to update. Generating UPDATE data...").arg(totalProgress));

    totalProgress *= 2; // two operations per playlist, generate data and apply data.

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
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      auto pathValue = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

      const std::filesystem::path playlistPath(pathValue);
      if(!std::filesystem::exists(playlistPath)) continue;
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
            qImage.scaledToHeight(x*32, Qt::SmoothTransformation);
            qImage.toPixelFormat(QImage::Format_RGB888);

            const auto blurHash = blurhash::encode(qImage.bits(), qImage.width(), qImage.height(), x, y);

            // Stack overflow: https://stackoverflow.com/questions/26109330/datetime-equivalent-in-c
            QFileInfo file(QString::fromStdWString(imagePath.wstring()));
            const auto writeTime = (file.lastModified().toMSecsSinceEpoch() * 10000) + 621355968000009999;

            entryData = std::filesystem::canonical(imagePath).string() + "*" + std::to_string(writeTime)
                + "*Primary*" + std::to_string(width) + "*" + std::to_string(height) + "*" + blurHash;

            std::cout << entryData << std::endl;
          }
          stbi_image_free(imageData);
        }
      }

      if(m_processArtists)
      {
        auto parts = QString::fromStdWString(playlistPath.stem().wstring()).split(SEPARATOR);
        if(parts.size() > 1)
        {
          artist = parts.takeFirst().toStdString();
          album = parts.join(SEPARATOR).toStdString();
        }
      }

      // TODO: comprobar artists + albums
      // TODO: UPDATE database.
      operations.emplace_back(playlistPath, entryData, artist, album);

      const int currentProgress = (++count * 100)/totalProgress;
      if(progressVal != currentProgress)
      {
        progressVal = currentProgress;
        emit progress(progressVal);
      }
    }

    if (result != SQLITE_DONE)
    {
      m_error = QString("Unable to finish SQL statement. SQLite3 error: %1").arg(QString::fromLatin1(sqlite3_errstr(result)));
    }
    sqlite3_finalize(stmt);
  }

  emit progress(100);
}
