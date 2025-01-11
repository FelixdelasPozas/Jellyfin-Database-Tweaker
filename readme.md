Jellyfin Metadata Database Tweaker
==================================

# Summary
- [Description](#description)
- [Compilation](#compilation-requirements)
- [Install](#install)
- [Screenshots](#screenshots)
- [Repository information](#repository-information)

# Description
Simple tool to modify the metadata database of a [Jellyfin](https://jellyfin.org/) server (that is, to **my** 
liking), mainly to insert image, artist and albums metadata and correct playlist contents to existing items. 
This tool doesn't insert new items in the database, just fills the missing information for audio albums, tracks
and playlists metadata. 

It's not recomended to do this and is **your** responsibility if you break your database using this application. I made 
it because I don't want to change the organization of my music collection for Jellyfin to "index it correctly", and I
want to update the information quickly every time I add more music to the collection.

[Jellyfin expects the music to be organized](https://github.com/jellyfin-archive/jellyfin-docs/blob/master/general/server/media/music.md) like this:
```
/Music
    /Artist
        /Album
            /Disc 1
                01 - Song.mp3
                02 - Song.mp3
            /Disc 2
                01 - Song.mp3
                02 - Song.mp3
            ...
            cover.jpg
```         
This tool expects this format:
```
/Music
    /Artist - Album
        01 - Song.mp3
        02 - Song.mp3
        ...
        Frontal.jpg

    Default.png                    
```            

Or, if multiple discs:
```
/Music
    /Artist - Album
        1-01 - Song.mp3
        1-02 - Song.mp3
        ...
        2-01 - Song.mp3
        2-02 - Song.mp3		
        ...
        Frontal.jpg

    Default.png                    
```     
       
Also the filenames of the tracks are expected to follow this rule `%disc number%-%track_number% - Track title.mp3`. 
Disc number being absent if it's just one disc. Both fields are supposed to be zero-filled to the total of discs and
tracks (that is, 001 for a disc with more than 100 tracks). The `Default.png` image is only used if none is found in the
album directory instead of letting the album and artist without one.   

Thanks to the people of the [Jellyfin](https://jellyfin.org/) project for making such a great program!

## Options
Several options can be configured:
* Playlist metadata: modify images with computed blurhash and add artist and album information.
* Playlist metadata: tracklist JSON content.
* Albums metadata: add artist and album information.
* Track metadata: sequential number in album.
* Track metadata: add artist and album information.

# Compilation requirements
## To build the tool:
* cross-platform build system: [CMake](http://www.cmake.org/cmake/resources/software.html).
* compiler: [Mingw64](http://sourceforge.net/projects/mingw-w64/) on Windows.

## External dependencies
The following libraries are required:
* [Qt 5 opensource framework](http://www.qt.io/).

The tool also requires of [BlurHash](https://github.com/Nheko-Reborn/blurhash), 
[stb_image](https://github.com/nothings/stb/blob/master/stb_image.h) and [SQLite 3](https://github.com/sqlite/sqlite) 
libraries but those are included in the **external** folder in the source code. stb_image code needed to be 
modified to allow UTF-8 filename strings with Mingw64 compiler as it wrongly uses `_MSC_VER_` macro to identify
a Windows(tm) machine and that excludes compilers others than Microsoft one.

# Install
There won't be binaries in the releases. If you want to use it you'll probably need to adapt it to your needs 
and compile it yourself, because this is rather specific to the organization of my own music collection. 

The latest release is available from the [releases]() page. 

# Screenshots
Simple main dialog. 

![maindialog](https://user-images.githubusercontent.com/12167134/266746374-006cbc3e-1ca5-4f72-adeb-b906efab8d48.png)

# Repository information

**Version**: 1.1.0

**Status**: finished. 

**cloc statistics** (excluding code in 'external' directory)

| Language                 |files     |blank    |comment   |code      |
|:-------------------------|---------:|--------:|---------:|---------:|
| C++                      |   4      | 301     |   152    | 1154     |
| C/C++ Header             |   3      |  74     |   221    |  143     |
| CMake                    |   1      |  18     |    15    |   64     |
| **Total**                | **8**    | **393** | **388**  | **1361** |
