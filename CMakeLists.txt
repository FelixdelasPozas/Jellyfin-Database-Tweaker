#
# Jellyfin Database Tweaker CMake configuration.
#
cmake_minimum_required (VERSION 3.0.0)

project(JellyfinDBTweaker)

cmake_policy(SET CMP0071 NEW)
set(CMAKE_CXX_STANDARD 20)

# Version Number
set (JELLYFINDBTWEAKER_VERSION_MAJOR 1)
set (JELLYFINDBTWEAKER_VERSION_MINOR 0)
set (JELLYFINDBTWEAKER_VERSION_PATCH 1)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

# Find the QtWidgets library
find_package(Qt5 COMPONENTS Widgets WinExtras)

# We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
#add_definitions(${Qt5Widgets_DEFINITIONS})
#add_definitions(${Qt5Multimedia_DEFINITIONS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${Qt5Multimedia_EXECUTABLE_COMPILE_FLAGS}")

if (CMAKE_BUILD_TYPE MATCHES Debug)
  set(CORE_EXTERNAL_LIBS ${CORE_EXTERNAL_LIBS} ${QT_QTTEST_LIBRARY})
endif (CMAKE_BUILD_TYPE MATCHES Debug)

if(DEFINED MINGW)
  configure_file("${PROJECT_SOURCE_DIR}/resources.rc.in" "${PROJECT_BINARY_DIR}/resources.rc")
  set(CORE_SOURCES ${CORE_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources.rc)
  set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> -i <SOURCE>")
  enable_language(RC)
endif(DEFINED MINGW)

# External sqlite code
set (SQLITE_FILES
  external/sqlite3/sqlite3.c
)
# External blurhash code
set (BLURHASH_FILES
  external/blurhash/blurhash.cpp
)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external # External code, not from the project.
  ${PROJECT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}          # Generated .h files
  ${CMAKE_CURRENT_BINARY_DIR}  # For wrap/ui files
  ${Qt5Widgets_INCLUDE_DIRS}
  ${Qt5WinExtras_INCLUDE_DIRS}
  )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-deprecated -std=c++20 -mwindows -m64")

# Add Qt Resource files
qt5_add_resources(RESOURCES
  rsc/resources.qrc
)

qt5_wrap_ui(CORE_UI
  # .ui for Qt
  MainDialog.ui
  AboutDialog.ui
)
	
set (CORE_SOURCES
  # project files
  ${CORE_SOURCES}
  ${RESOURCES}
  ${CORE_MOC}
  ${CORE_UI}
  ${SQLITE_FILES}
  ${BLURHASH_FILES}
  Main.cpp
  MainDialog.cpp
  AboutDialog.cpp
  ProcessThread.cpp
)

set(CORE_EXTERNAL_LIBS
  Qt5::Widgets
  Qt5::WinExtras
)
  
add_executable(JellyfinDBTweaker ${CORE_SOURCES})
target_link_libraries (JellyfinDBTweaker ${CORE_EXTERNAL_LIBS})
qt5_use_modules(JellyfinDBTweaker Widgets)
