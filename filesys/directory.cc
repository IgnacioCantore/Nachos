/// Routines to manage a directory of file names.
///
/// The directory is a table of fixed length entries; each entry represents a
/// single file, and contains the file name, and the location of the file
/// header on disk.  The fixed size of each directory entry means that we
/// have the restriction of a fixed maximum size for file names.
///
/// The constructor initializes an empty directory of a certain size; we use
/// ReadFrom/WriteBack to fetch the contents of the directory from disk, and
/// to write back any modifications back to disk.
///
/// Also, this implementation has the restriction that the size of the
/// directory cannot expand.  In other words, once all the entries in the
/// directory are used, no more files can be created.  Fixing this is one of
/// the parts to the assignment.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "directory.hh"
#include "directory_entry.hh"
#include "file_header.hh"
#include "file_system.hh"
#include "lib/utility.hh"

#include <stdio.h>
#include <string.h>

static const unsigned NEW_DIR_ENTRIES = 5;

/// Initialize a directory; initially, the directory is completely empty.  If
/// the disk is being formatted, an empty directory is all we need, but
/// otherwise, we need to call FetchFrom in order to initialize it from disk.
///
/// * `size` is the number of entries in the directory.
Directory::Directory(unsigned size)
{
    raw.tableSize = size;
    if (size != 0) {  // It's a newly created directory.
        raw.table = new DirectoryEntry [size];
        for (unsigned i = 0; i < raw.tableSize; i++)
            raw.table[i].inUse = false;
    } else  // It's an existing directory, data has to be fetched from disk.
        raw.table = nullptr;
}

/// De-allocate directory data structure.
Directory::~Directory()
{
    ASSERT(raw.table != nullptr);
    delete [] raw.table;
}

/// Read the contents of the directory from disk.
///
/// * `file` is file containing the directory contents.
void
Directory::FetchFrom(OpenFile *file)
{
    ASSERT(file != nullptr);
    unsigned fileSize = file->Length();
    unsigned tableSize = fileSize / sizeof (DirectoryEntry);

    ASSERT(tableSize > raw.tableSize);

    raw.table = new DirectoryEntry [tableSize];
    raw.tableSize = tableSize;

    file->ReadAt((char *) raw.table, fileSize, 0);
}

/// Write any modifications to the directory back to disk.
///
/// * `file` is a file to contain the new directory contents.
void
Directory::WriteBack(OpenFile *file)
{
    ASSERT(file != nullptr);
    unsigned fileSize = file->Length();

    ASSERT(fileSize == raw.tableSize * sizeof (DirectoryEntry));

    file->WriteAt((char *) raw.table, fileSize, 0);
}

/// Look up file/directory name in directory, and return its location in the
/// table of directory entries. Return -1 if the name is not in the directory.
///
/// * `name` is the file/directory name to look up.
int
Directory::FindIndex(const char *name)
{
    ASSERT(name != nullptr);

    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse
              && !strncmp(raw.table[i].name, name, FILE_NAME_MAX_LEN))
            return i;
    return -1;  // name not in directory
}

/// Look up file/directory name in directory, and return the disk sector number
/// where the file/directory's header is stored.  Return -1 if the name is not
/// in the directory.
///
/// * `name` is the file/directory name to look up.
int
Directory::Find(const char *name)
{
    ASSERT(name != nullptr);

    int i = FindIndex(name);
    if (i != -1)
        return raw.table[i].sector;
    return -1;
}

/// Add a file/directory into the directory. Return true if successful; return
/// false if the name is already in the directory, or if the directory is
/// completely full, and has no more space for additional file names.
///
/// * `name` is the name of the file/directory being added.
/// * `sector` is the disk sector containing the added file's header.
/// * `isDir` indicates if what's to be added is a directory or a file.
/// * `freeMap` is the bit map of free disk sectors.
/// * `dirH` is this directory's file header.
bool
Directory::Add(const char *name, int sector, bool isDir, Bitmap *freeMap,
               DirSynch *dirSynch)
{
    ASSERT(name != nullptr);

    if (FindIndex(name) != -1)
        return false;

    unsigned i = 0;
    for (; i < raw.tableSize; i++)
        if (!raw.table[i].inUse) {
            raw.table[i].inUse = true;
            strncpy(raw.table[i].name, name, FILE_NAME_MAX_LEN);
            raw.table[i].sector = sector;
            raw.table[i].isDir = isDir;
            return true;
        }

    if (ExpandDirectory(freeMap, dirSynch)) {  // Directory expanded, add file.
        ASSERT(!raw.table[i].inUse);
        raw.table[i].inUse = true;
        strncpy(raw.table[i].name, name, FILE_NAME_MAX_LEN);
        raw.table[i].sector = sector;
        raw.table[i].isDir = isDir;
        return true;

    }
    return false;  // no space.
}

/// Remove a file name from the directory.   Return true if successful;
/// return false if the file is not in the directory.
///
/// * `name` is the file or directory name to be removed.
bool
Directory::Remove(const char *name)
{
    ASSERT(name != nullptr);

    int i = FindIndex(name);
    if (i == -1)
        return false;  // name not in directory
    raw.table[i].inUse = false;
    return true;
}

/// Returns `true` if the directory entry with the given name is a directory.
/// Otherwise, or if there is no entry with that name, returns `false`.
///
/// * `name` is the entry to check.
bool
Directory::IsDir(const char *name)
{
    ASSERT(name != nullptr);

    int i = FindIndex(name);
    if (i != -1)
        return raw.table[i].isDir;
    return false;
}

/// Check if this directory is empty.
bool
Directory::IsEmpty()
{
    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse)
            return false;
    return true;
}

/// List all the file and directory names in this directory and in its
/// subdirectories.
void
Directory::List(const char *path) const
{
    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse) {
            printf("%s", raw.table[i].name);
            if (raw.table[i].isDir)
                printf("/\n");
            else
                printf("\n");
        }
    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse && raw.table[i].isDir) {
            Directory *dir = new Directory(0);
            OpenFile *dirFile = new OpenFile(raw.table[i].sector);

            char dirPath[strlen(path) + strlen(raw.table[i].name) + 2];
            strncpy(dirPath, path, strlen(path) + 1);
            strncat(dirPath, "/", 2);
            strncat(dirPath, raw.table[i].name, strlen(raw.table[i].name) + 1);
            printf("\n%s:\n", dirPath);
            dir->FetchFrom(dirFile);
            dir->List(dirPath);
            delete dir;
            delete dirFile;
        }
}

/// List all the file and directory names in this directory and in its
/// subdirectories, their `FileHeader` locations, and the contents of each
/// file. For debugging.
void
Directory::Print(const char *path) const
{
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse) {
            char type[10];
            if (raw.table[i].isDir)
                strncpy(type, "directory", 10);
            else
                strncpy(type, "file", 5);
            printf("\nDirectory entry:\n"
                   "    name: %s\n"
                   "    sector: %u\n"
                   "    type: %s\n",
                   raw.table[i].name, raw.table[i].sector, type);
            hdr->FetchFrom(raw.table[i].sector);
            hdr->Print(nullptr);
        }
    printf("\n");
    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse && raw.table[i].isDir) {
            Directory *dir    = new Directory(0);
            OpenFile *dirFile = new OpenFile(raw.table[i].sector);

            char dirPath[strlen(path) + strlen(raw.table[i].name) + 2];
            strncpy(dirPath, path, strlen(path) + 1);
            strncat(dirPath, "/", 2);
            strncat(dirPath, raw.table[i].name, strlen(raw.table[i].name) + 1);

            printf("--------------------------------\n");
            printf("--- Directory path: %s\n", dirPath);
            dir->FetchFrom(dirFile);
            dir->Print(dirPath);
            delete dir;
            delete dirFile;
        }
    delete hdr;
}

const RawDirectory *
Directory::GetRaw() const
{
    return &raw;
}

/// Expand the directory if there's no free directory entry for a new file
/// or directory.
///
/// * `freeMap` is the bit map of free disk sectors.
/// * `dirSynch` is this directory's synchronization structure.
bool
Directory::ExpandDirectory(Bitmap *freeMap, DirSynch *dirSynch)
{
    FileHeader *dirH = dirSynch->GetHeader();
    bool success = dirH->Expand(freeMap,
                                NEW_DIR_ENTRIES * sizeof (DirectoryEntry));
    if (success) {
        dirH->WriteBack(dirSynch->GetSector());
        unsigned oldSize = raw.tableSize;
        unsigned newSize = oldSize + NEW_DIR_ENTRIES;

        DirectoryEntry *newTable = new DirectoryEntry [newSize];
        memcpy(newTable, raw.table, oldSize * sizeof (DirectoryEntry));

        for (unsigned i = oldSize; i < newSize; i++)
          newTable[i].inUse = false;

        delete [] raw.table;
        raw.table = newTable;
        raw.tableSize = newSize;
    }
    return success;
}

/// Return the first swap file in this directory. Used for cleanup when Nachos
/// is booting up.
char *
Directory::FindSwapFile()
{
    for (unsigned i = 0; i < raw.tableSize; i++)
        if (raw.table[i].inUse && !strncmp(raw.table[i].name, "SWAP.", 5)) {
            ASSERT(!raw.table[i].isDir);
            raw.table[i].inUse = false;
            return raw.table[i].name;
        }
    return nullptr;
}
