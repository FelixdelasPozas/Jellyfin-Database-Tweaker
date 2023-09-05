Jellyfin Metadata Database Tweaker
==================================

# Summary
- [Description](#description)
- [Compilation](#compilation-requirements)
- [Install](#install)
- [Screenshots](#screenshots)
- [Repository information](#repository-information)

# Description
Quick and simple tool to modify the metadata database of a Jellyfin server (that is, to **my** liking), mainly to 
insert image, artist and albums metadata to existing items. This tool doesn't insert new items in the database, 
just fills the missing information. 

It's not recomended to do this and is **your** responsibility if you break your database using this application. I made 
it because I don't want to change the organization of my music collection for Jellyfin to "index it correctly", and I
want to update the information quickly every time I add more music to the collection.

## Options
Several options can be configured:
* Modify only images in albums/playlist entities, with the name of the image to search in the folder.
* Add artist and album metadata in all songs, playlist and album entries.

# Compilation requirements
## To build the tool:
* cross-platform build system: [CMake](http://www.cmake.org/cmake/resources/software.html).
* compiler: [Mingw64](http://sourceforge.net/projects/mingw-w64/) on Windows.

## External dependencies
The following libraries are required:
* [Qt 5 opensource framework](http://www.qt.io/).

The tool also requires of BlurHash, stb_image and SQLite 3 libraries but those are included in the **external** folder in the
source code. stb_image code needed to be modified to allow UTF-8 filename strings with Mingw64 compiler as it wrongly uses
_MSC_VER_ macro to identify a Windows(tm) machine. 

# Install
There won't be binaries in the releases. If you want to use it you'll probably need to adapt it to your needs 
and compile it yourself, because this is rather specific to the organization of my own music collection. 

The latest release is available from the [releases]() page. 

# Screenshots
Simple main dialog. 

![maindialog]()

# Repository information

**Version**: 1.0.0

**Status**: Probably will do some changes in the future to add metadata instead of just modifying the existing one. Add artists
and correctly link albums to songs. 

**cloc statistics**

| Language                     |files          |blank        |comment           |code  |
|:-----------------------------|--------------:|------------:|-----------------:|-----:|
| C++                          |   8           | 438         |   272            | 1681 |
| C/C++ Header                 |   7           | 200         |   604            |  336 |
| CMake                        |   1           |  18         |    14            |   65 |
| **Total**                    | **16**        | **656**     | **890**          | **2082** |
