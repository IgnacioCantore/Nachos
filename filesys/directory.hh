/// Data structures to manage a UNIX-like directory of file names.
///
/// A directory is a table of pairs: *<file name, sector #>*, giving the name
/// of each file in the directory, and where to find its file header (the
/// data structure describing where to find the file's data blocks) on disk.
///
/// We assume mutual exclusion is provided by the caller.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.

#ifndef NACHOS_FILESYS_DIRECTORY__HH
#define NACHOS_FILESYS_DIRECTORY__HH


#include "raw_directory.hh"
#include "open_file.hh"
#include "fs_synch.hh"
#include "lib/bitmap.hh"


/// The following class defines a UNIX-like “directory”.  Each entry in the
/// directory describes a file, and where to find it on disk.
///
/// The directory data structure can be stored in memory, or on disk.  When
/// it is on disk, it is stored as a regular Nachos file.
///
/// The constructor initializes a directory structure in memory; the
/// `FetchFrom`/`WriteBack` operations shuffle the directory information
/// from/to disk.
class Directory {
public:

    /// Initialize an empty directory with space for `size` files.
    Directory(unsigned size);

    /// De-allocate the directory.
    ~Directory();

    /// Initialize directory contents from disk.
    void FetchFrom(OpenFile *file);

    /// Write modifications to directory contents back to disk.
    void WriteBack(OpenFile *file);

    /// Find the sector number of the `FileHeader` for file/directory: `name`.
    int Find(const char *name);

    /// Add a file/directory name into the directory.
    bool Add(const char *name, int sector, bool isDir, Bitmap *bitMap,
             DirSynch *dirSynch);

    /// Remove a file from the directory.
    bool Remove(const char *name);

    /// Is the directory entry with `name` a directory or a file?
    bool IsDir(const char *name);

    /// Is this directory empty?
    bool IsEmpty();

    /// Print the names of all the files in this directory and its
    /// subdirectories.
    void List(const char *path) const;

    /// Verbose print of the contents of this directory and its subdirectories
    /// -- all the file names and their contents.
    void Print(const char *path) const;

    /// Find and return the first swap file in this directory.
    char *FindSwapFile();

    /// Get the raw directory structure.
    ///
    /// NOTE: this should only be used by routines that operating on the file
    /// system at a low level.
    const RawDirectory *GetRaw() const;

private:

    /// Find the index into the directory table corresponding to `name`.
    int FindIndex(const char *name);
    
    /// Expand the directory when all of its entries are full.
    bool ExpandDirectory(Bitmap *bitMap, DirSynch *dirSynch);

    RawDirectory raw;
};


#endif
