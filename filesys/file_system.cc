/// Routines to manage the overall operation of the file system.  Implements
/// routines to map from textual file names to files.
///
/// Each file in the file system has:
/// * a file header, stored in a sector on disk (the size of the file header
///   data structure is arranged to be precisely the size of 1 disk sector);
/// * a number of data blocks;
/// * an entry in the file system directory.
///
/// The file system consists of several data structures:
/// * A bitmap of free disk sectors (cf. `bitmap.h`).
/// * A directory of file names and file headers.
///
/// Both the bitmap and the directory are represented as normal files.  Their
/// file headers are located in specific sectors (sector 0 and sector 1), so
/// that the file system can find them on bootup.
///
/// The file system assumes that the bitmap and directory files are kept
/// “open” continuously while Nachos is running.
///
/// For those operations (such as `Create`, `Remove`) that modify the
/// directory and/or bitmap, if the operation succeeds, the changes are
/// written immediately back to disk (the two files are kept open during all
/// this time).  If the operation fails, and we have modified part of the
/// directory and/or bitmap, we simply discard the changed version, without
/// writing it back to disk.
///
/// Our implementation at this point has the following restrictions:
///
/// * there is no synchronization for concurrent accesses;
/// * files have a fixed size, set when the file is created;
/// * files cannot be bigger than about 3KB in size;
/// * there is no hierarchical directory structure, and only a limited number
///   of files can be added to the system;
/// * there is no attempt to make the system robust to failures (if Nachos
///   exits in the middle of an operation that modifies the file system, it
///   may corrupt the disk).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "file_system.hh"
#include "directory.hh"
#include "directory_entry.hh"
#include "file_header.hh"
#include "fs_synch.hh"
#include "fs_synch_list.hh"
#include "lib/bitmap.hh"
#include "machine/disk.hh"
#include "threads/system.hh"

#include <stdio.h>
#include <string.h>


/// Initial file sizes for the bitmap and directory; until the file system
/// supports extensible files, the directory size sets the maximum number of
/// files that can be loaded onto the disk.
static const unsigned FREE_MAP_FILE_SIZE = NUM_SECTORS / BITS_IN_BYTE;
static const unsigned NUM_DIR_ENTRIES = 10;
static const unsigned DIRECTORY_FILE_SIZE = sizeof (DirectoryEntry)
                                            * NUM_DIR_ENTRIES;

/// Initialize the file system.  If `format == true`, the disk has nothing on
/// it, and we need to initialize the disk to contain an empty directory, and
/// a bitmap of free sectors (with almost but not all of the sectors marked
/// as free).
///
/// If `format == false`, we just have to open the files representing the
/// bitmap and the directory.
///
/// * `format` -- should we initialize the disk?
FileSystem::FileSystem(bool format)
{
    OpenFile *freeMapFile;
    OpenFile *rootDirectoryFile;

    DEBUG('f', "Initializing the file system.\n");
    if (format) {
        Bitmap     *freeMap = new Bitmap(NUM_SECTORS);
        Directory  *dir     = new Directory(NUM_DIR_ENTRIES);
        FileHeader *mapH    = new FileHeader;
        FileHeader *dirH    = new FileHeader;

        DEBUG('f', "Formatting the file system.\n");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
        freeMap->Mark(FREE_MAP_SECTOR);
        freeMap->Mark(DIRECTORY_SECTOR);

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

        ASSERT(mapH->Allocate(freeMap, FREE_MAP_FILE_SIZE));
        ASSERT(dirH->Allocate(freeMap, DIRECTORY_FILE_SIZE));

        // Flush the bitmap and directory `FileHeader`s back to disk.
        // We need to do this before we can `Open` the file, since open reads
        // the file header off of disk (and currently the disk has garbage on
        // it!).

        DEBUG('f', "Writing headers back to disk.\n");
        mapH->WriteBack(FREE_MAP_SECTOR);
        dirH->WriteBack(DIRECTORY_SECTOR);

        // OK to open the bitmap and directory files now.
        // The file system operations assume these two files are left open
        // while Nachos is running.

        freeMapFile       = new OpenFile(FREE_MAP_SECTOR);
        rootDirectoryFile = new OpenFile(DIRECTORY_SECTOR);

        // Once we have the files “open”, we can write the initial version of
        // each file back to disk.  The directory at this point is completely
        // empty; but the bitmap has been changed to reflect the fact that
        // sectors on the disk have been allocated for the file headers and
        // to hold the file data for the directory and bitmap.

        DEBUG('f', "Writing bitmap and directory back to disk.\n");
        freeMap->WriteBack(freeMapFile);  // Flush changes to disk
        dir->WriteBack(rootDirectoryFile);

        if (debug.IsEnabled('f')) {
            freeMap->Print();
            dir->Print("");
        }
        delete freeMap;
        delete dir;
        delete mapH;
        delete dirH;
    } else {
        // If we are not formatting the disk, just open the files
        // representing the bitmap and directory; these are left open while
        // Nachos is running.
        freeMapFile       = new OpenFile(FREE_MAP_SECTOR);
        rootDirectoryFile = new OpenFile(DIRECTORY_SECTOR);
    }
    fsSynchList = new FSSynchList();
    fsSynchList->AcquireLock();
    fsSynchList->Add(freeMapFile, FREE_MAP_SECTOR);
    fsSynchList->Add(rootDirectoryFile, DIRECTORY_SECTOR);
    fsSynchList->ReleaseLock();
}

FileSystem::~FileSystem()
{
    while (!fsSynchList->IsEmpty()) {
        FSSynch *fileSysSynch = fsSynchList->Pop();
        delete fileSysSynch;
    }
    delete fsSynchList;
}

/// Create a file in the Nachos file system (similar to UNIX `create`).
/// Since we cannot increase the size of files dynamically, we have to give
/// Create the initial size of the file.
///
/// The steps to create a file are:
/// 1. Make sure the file does not already exist.
/// 2. Allocate a sector for the file header.
/// 3. Allocate space on disk for the data blocks for the file.
/// 4. Add the name to the directory.
/// 5. Store the new file header on disk.
/// 6. Flush the changes to the bitmap and the directory back to disk.
///
/// Return true if everything goes ok, otherwise, return false.
///
/// Create fails if:
/// * file is already in directory;
/// * no free space for file header;
/// * no free entry for file in directory;
/// * no free space for data blocks for the file.
///
/// Note that this implementation assumes there is no concurrent access to
/// the file system!
///
/// * `path` is the path of the file or the directory to be created.
/// * `initialSize` is the size of file to be created.
/// * `isDir` indicates if what's being created is a directory or a file.
bool
FileSystem::Create(const char *path, unsigned initialSize, bool isDir)
{
    ASSERT(path != nullptr);

    if (isDir)
        DEBUG('f', "Creating directory %s\n", path);
    else
        DEBUG('f', "Creating file %s, size %u\n", path, initialSize);

    char dirPath[strlen(path) + 1], name[strlen(path) + 1];
    SplitPath(path, dirPath, name);
    DirSynch *dirSynch = FindDirectory(dirPath);
    if (dirSynch == nullptr)
        return false;  // Couldn't find some directory in the path.

    // We don't allow directory names beggining with `SWAP.` in root
    // directory, because they may cause trouble with SWAP files.
    if (dirSynch == fsSynchList->Get(DIRECTORY_SECTOR) && isDir &&
        strncmp("SWAP.", name, 5) == 0)
        return false;

    Directory *dir = new Directory(0);
    dirSynch->AcquireLock();
    dir->FetchFrom(dirSynch->GetFile());

    bool success;

    if (dir->Find(name) != -1)
        success = false;  // `name` is already in directory.
    else {
        Bitmap *freeMap = new Bitmap(NUM_SECTORS);
        FreeMapSynch *freeMapSynch = fsSynchList->Get(FREE_MAP_SECTOR);
        freeMapSynch->AcquireLock();
        freeMap->FetchFrom(freeMapSynch->GetFile());
        int sector = freeMap->Find();
          // Find a sector to hold the file header.
        if (sector == -1)
            success = false;  // No free block for file header.
        else if (!dir->Add(name, sector, isDir, freeMap, dirSynch))
            success = false;  // No space in directory.
        else {
            FileHeader *h = new FileHeader;
            if (isDir)
                success = h->Allocate(freeMap, DIRECTORY_FILE_SIZE);
            else
                success = h->Allocate(freeMap, initialSize);
              // Fails if no space on disk for data.
            if (success) {
                // Everything worked, flush all changes back to disk.
                h->WriteBack(sector);
                dir->WriteBack(dirSynch->GetFile());
                freeMap->WriteBack(freeMapSynch->GetFile());
                if (isDir) {
                    OpenFile *newDirFile = new OpenFile(sector);
                    Directory *newDir = new Directory(NUM_DIR_ENTRIES);
                    newDir->WriteBack(newDirFile);
                    delete newDir;
                    delete newDirFile;
                }
            }
            delete h;
        }
        freeMapSynch->ReleaseLock();
        delete freeMap;
    }
    dirSynch->ReleaseLock();
    delete dir;
    return success;
}

/// Open a file for reading and writing.
///
/// To open a file:
/// 1. Find the location of the file's header, using the directory.
/// 2. Bring the header into memory.
///
/// * `path` is the path string of the file to be opened.
OpenFile *
FileSystem::Open(const char *path)
{
    ASSERT(path != nullptr);

    char dirPath[strlen(path) + 1], name[strlen(path) + 1];
    SplitPath(path, dirPath, name);
    DirSynch *dirSynch = FindDirectory(dirPath);
    if (dirSynch == nullptr)
        return nullptr;  // Couldn't find some directory in the path.

    Directory *dir = new Directory(0);
    dirSynch->AcquireLock();
    dir->FetchFrom(dirSynch->GetFile());
    OpenFile *openFile = nullptr;

    DEBUG('f', "Opening file %s\n", path);
    int sector = dir->Find(name);
    if (sector >= 0 && !dir->IsDir(name)) {  // `name` was found in directory,
        bool beingRemoved = false;           // and it's a file.
        fileSynchList->AcquireLock();
        FileSynch *fileSynch = fileSynchList->Get(sector);

        if (fileSynch == nullptr)
            fileSynchList->Add(path, sector);
        else
            beingRemoved = fileSynch->FileOpened();
        fileSynchList->ReleaseLock();
        if (!beingRemoved)
            openFile = new OpenFile(sector);
    }
    dirSynch->ReleaseLock();
    delete dir;
    return openFile;  // Return null if not found, file is pending of removal
                      // or trying to open a directory.
}

/// Delete a file/directory from the file system.
///
/// This requires:
/// 1. Remove it from the directory.
/// 2. Delete the space for its header.
/// 3. Delete the space for its data blocks.
/// 4. Write changes to directory, bitmap back to disk.
///
/// Return true if the file/directory was deleted, false if the file/directory
/// was not in the file system or if the directory to be removed is not empty.
///
/// * `path` is the path string of the file/directory to be removed.
bool
FileSystem::Remove(const char *path)
{
    ASSERT(path != nullptr);

    char dirPath[strlen(path) + 1], name[strlen(path) + 1];
    SplitPath(path, dirPath, name);
    DirSynch *dirSynch = FindDirectory(dirPath);
    if (dirSynch == nullptr)
        return false;  // Couldn't find some directory in the path.

    Directory *dir = new Directory(0);
    dirSynch->AcquireLock();
    dir->FetchFrom(dirSynch->GetFile());

    FileSynch *fileToRemoveSynch = nullptr;
    DirSynch *dirToRemoveSynch = nullptr;
    bool success, isDir;
    int sector = dir->Find(name);
    if (sector == -1)  // `name` not found.
        success = false;
    else if ((isDir = dir->IsDir(name))) {  // `name` is a directory.
        Directory *dirToRemove = new Directory(0);
        fsSynchList->AcquireLock();
        dirToRemoveSynch = fsSynchList->Get(sector);
        OpenFile *dirToRemoveFile = nullptr;
        if (dirToRemoveSynch != nullptr)
            dirToRemoveFile = dirToRemoveSynch->GetFile();
        else
            dirToRemoveFile = new OpenFile(sector);
        dirToRemove->FetchFrom(dirToRemoveFile);
        if (!dirToRemove->IsEmpty()) {
            success = false;  // directory `name` is not empty.
            if (dirToRemoveSynch == nullptr)  // We created a new OpenFile for
                delete dirToRemoveFile;       // this, so we have to delete it.
        } else {
            success = true;
            // If the directory being removed has been opened, remove it from
            // the directory synch list.
            if (dirToRemoveSynch != nullptr) {
                fsSynchList->Remove(dirToRemoveSynch);
                delete dirToRemoveSynch;
            }
        }
        fsSynchList->ReleaseLock();
        delete dirToRemove;
    } else {  // `name` is a file.
        success = true;
        fileSynchList->AcquireLock();
        fileToRemoveSynch = fileSynchList->Get(sector);
        fileSynchList->ReleaseLock();
    }
    if (success) {
        if (fileToRemoveSynch == nullptr) {      // It's an empty directory or
            FileHeader *fileH = new FileHeader;  // a file that isn't opened by
            fileH->FetchFrom(sector);            // any thread, so delete it.

            Bitmap *freeMap = new Bitmap(NUM_SECTORS);
            FreeMapSynch *freeMapSynch = fsSynchList->Get(FREE_MAP_SECTOR);
            freeMapSynch->AcquireLock();
            freeMap->FetchFrom(freeMapSynch->GetFile());

            fileH->Deallocate(freeMap);  // Remove data blocks.
            freeMap->Clear(sector);      // Remove header block.
            dir->Remove(name);

            freeMap->WriteBack(freeMapSynch->GetFile());  // Flush to disk.
            freeMapSynch->ReleaseLock();
            dir->WriteBack(dirSynch->GetFile());      // Flush to disk.
            delete fileH;
            delete freeMap;
        } else {
            fileToRemoveSynch->SetToRemove();
        }
    }
    dirSynch->ReleaseLock();
    delete dir;
    return success;
}

/// Expand a file by allocating more sectors to its file header. If it
/// succeeds, flush the changes to disk.
///
/// * `sector` is the disk sector containing the file header.
/// * `numBytes` indicates how many bytes the file is being increased.
bool
FileSystem::ExpandFile(unsigned sector, unsigned numBytes)
{
    ASSERT(numBytes != 0);

    fileSynchList->AcquireLock();
    FileSynch *fileSynch = fileSynchList->Get(sector);
    fileSynchList->ReleaseLock();
    FileHeader *fileH = fileSynch->GetFileHeader();

    Bitmap *freeMap = new Bitmap(NUM_SECTORS);
    FreeMapSynch *freeMapSynch = fsSynchList->Get(FREE_MAP_SECTOR);
    freeMapSynch->AcquireLock();
    freeMap->FetchFrom(freeMapSynch->GetFile());

    bool success = fileH->Expand(freeMap, numBytes);
    if (success) {
        fileH->WriteBack(sector);
        freeMap->WriteBack(freeMapSynch->GetFile());
    }
    freeMapSynch->ReleaseLock();
    delete freeMap;
    return success;
}

/// Split a `path` string into two strings: a directory path string, and the
/// name of the file/directory.
///
/// * `path` is the source string, containing the path.
/// * `dirPath` is a pointer to the string where the directory path will be
///   copied.
/// * `name` is a pointer to the string where the name will be copied.
void
FileSystem::SplitPath(const char *path, char *dirPath, char *name)
{
    ASSERT(path != nullptr);
    ASSERT(dirPath != nullptr);
    ASSERT(name != nullptr);

    strncpy(dirPath, path, strlen(path) + 1);
    char *lastChar = &dirPath[strlen(dirPath) - 1];
    if (lastChar != dirPath && *lastChar == '/')
        *lastChar = '\0';  // If there's a '/' at the end of the path and it's
                           // not the only char in the string, remove it.
    char *firstSlash = strchr(dirPath, '/');  // First occurence of '/'.
    if (firstSlash == nullptr) {  // There's no '/' in the path.
        strncpy(name, dirPath, strlen(dirPath) + 1);
        dirPath[0] = '\0';  // Dir path is empty.
    } else {  // There's at least one '/'.
        char *lastSlash = strrchr(dirPath, '/');  // Last occurence of '/'.
        strncpy(name, lastSlash + 1, strlen(lastSlash + 1) + 1);
        if (lastSlash == dirPath)  // The dir path is the root directory.
            *(lastSlash + 1) = '\0';
        else
            *lastSlash = '\0';
    }
}

/// Find the directory given by `dirPath`, while "opening" every directory in
/// the path (adding them to the FSSynchList), and return its DirSynch. If some
/// directory is not found, return null.
///
/// * `dirPath` is the path string.
DirSynch *
FileSystem::FindDirectory(char *dirPath)
{
    ASSERT(dirPath != nullptr);

    DirSynch *dirSynch = nullptr;
    if (dirPath[0] == '/')  // Absolute path
        dirSynch = fsSynchList->Get(DIRECTORY_SECTOR);
    else                    // Relative path
        dirSynch = currentThread->GetCurrentDir();

    ASSERT(dirSynch != nullptr);

    char *token = strtok(dirPath, "/");
    while (token != nullptr && dirSynch != nullptr) {  // Iterates over each
        Directory *dir = new Directory(0);             // subdir in the path.
        dirSynch->AcquireLock();
        dir->FetchFrom(dirSynch->GetFile());
        if (!dir->IsDir(token)) {  // Directory not found or it's a file.
            delete dir;
            dirSynch->ReleaseLock();
            dirSynch = nullptr;
        } else {
            int sector = dir->Find(token);
            delete dir;
            fsSynchList->AcquireLock();
            DirSynch *newDirSynch = fsSynchList->Get(sector);
            if (newDirSynch == nullptr) {  // Subdirectory hasn't been opened.
                OpenFile *newDirFile = new OpenFile(sector);
                newDirSynch = fsSynchList->Add(newDirFile, sector);
            }
            fsSynchList->ReleaseLock();
            dirSynch->ReleaseLock();
            dirSynch = newDirSynch;
        }
        token = strtok(nullptr, "/");
    }
    return dirSynch;
}

/// Clean SWAP files from the root directory.
void
FileSystem::Cleanup()
{
    DirSynch *rootDirSynch = fsSynchList->Get(DIRECTORY_SECTOR);
    Directory *rootDir = new Directory(0);
    rootDir->FetchFrom(rootDirSynch->GetFile());
    char *swapFilename = rootDir->FindSwapFile();
    while (swapFilename != nullptr) {
        Remove(swapFilename);
        swapFilename = rootDir->FindSwapFile();
    }
    delete rootDir;
}

/// List all the files in the file system directories.
void
FileSystem::List()
{
    printf("/:\n");
    Directory *dir = new Directory(0);
    OpenFile *directoryFile = fsSynchList->Get(DIRECTORY_SECTOR)->GetFile();
    dir->FetchFrom(directoryFile);
    dir->List("");
    delete dir;
}

static bool
AddToShadowBitmap(unsigned sector, Bitmap *map)
{
    ASSERT(map != nullptr);

    if (map->Test(sector)) {
        DEBUG('f', "Sector %u was already marked.\n", sector);
        return false;
    }
    map->Mark(sector);
    DEBUG('f', "Marked sector %u.\n", sector);
    return true;
}

static bool
CheckForError(bool value, const char *message)
{
    if (!value)
        DEBUG('f', message);
    return !value;
}

static bool
CheckSector(unsigned sector, Bitmap *shadowMap)
{
    bool error = false;

    error |= CheckForError(sector < NUM_SECTORS, "Sector number too big.\n");
    error |= CheckForError(AddToShadowBitmap(sector, shadowMap),
                           "Sector number already used.\n");
    return error;
}

static bool
CheckFileHeader(FileHeader *h, unsigned num, Bitmap *shadowMap)
{
    ASSERT(h != nullptr);

    const RawFileHeader *rh = h->GetRaw();

    ASSERT(rh != nullptr);

    bool error = false;

    DEBUG('f', "Checking file header %u.  File size: %u bytes, number of sectors: %u.\n",
          num, rh->numBytes, rh->numSectors);
    error |= CheckForError(rh->numSectors >= DivRoundUp(rh->numBytes,
                                                        SECTOR_SIZE),
                           "Sector count not compatible with file size.\n");
    error |= CheckForError(rh->numSectors < NUM_DIRECT +
                           NUM_INDIRECT * NUM_INDIRECT, "Too many blocks.\n");

    for (unsigned i = 0; i < rh->numSectors; i++) {
        unsigned s = h->ByteToSector(i * SECTOR_SIZE);
        error |= CheckSector(s, shadowMap);
    }

    int indirSector = rh->indirSector;

    if (indirSector != -1) {
        error |= CheckSector(indirSector, shadowMap);
        int firstIndir[NUM_INDIRECT];
        synchDisk->ReadSector(indirSector, (char *) firstIndir);
        for (unsigned i = 0; i < NUM_INDIRECT && firstIndir[i] != -1; i++)
            error |= CheckSector(firstIndir[i], shadowMap);
    }
    return error;
}

static bool
CheckBitmaps(const Bitmap *freeMap, const Bitmap *shadowMap)
{
    bool error = false;
    for (unsigned i = 0; i < NUM_SECTORS; i++) {
        DEBUG('f', "Checking sector %u. Original: %u, shadow: %u.\n",
              i, freeMap->Test(i), shadowMap->Test(i));
        error |= CheckForError(freeMap->Test(i) == shadowMap->Test(i),
                               "Inconsistent bitmap.\n");
    }
    return error;
}

static bool
CheckDirectory(const RawDirectory *rd, Bitmap *shadowMap, const char *path)
{
    ASSERT(rd != nullptr);
    ASSERT(shadowMap != nullptr);

    bool error = false;
    unsigned nameCount = 0;
    unsigned size = rd->tableSize;
    const char *knownNames[size];

    for (unsigned i = 0; i < size; i++) {
        DEBUG('f', "Checking direntry: %u.\n", i);
        const DirectoryEntry *e = &rd->table[i];

        if (e->inUse) {
            if (strlen(e->name) > FILE_NAME_MAX_LEN) {
                DEBUG('f', "%s too long.\n", e->isDir ? "Directory name":
                      "Filename");
                error = true;
            }

            // Check for repeated filenames.
            DEBUG('f', "Checking for repeated names.  Name count: %u.\n",
                  nameCount);
            bool repeated = false;
            for (unsigned j = 0; j < nameCount; j++) {
                DEBUG('f', "Comparing \"%s\" and \"%s\".\n",
                      knownNames[j], e->name);
                if (strcmp(knownNames[j], e->name) == 0) {
                    DEBUG('f', "Repeated file or directory name.\n");
                    repeated = true;
                    error = true;
                }
            }
            if (!repeated) {
                knownNames[nameCount] = e->name;
                DEBUG('f', "Added \"%s\" at %u.\n", e->name, nameCount);
                nameCount++;
            }

            // Check sector.
            error |= CheckSector(e->sector, shadowMap);

            // Check file header.
            FileHeader *h = new FileHeader;
            h->FetchFrom(e->sector);
            error |= CheckFileHeader(h, e->sector, shadowMap);
            delete h;

            // If this entry is a directory, check its content.
            if (e->isDir) {
                char dirPath[strlen(path) + strlen(e->name) + 2];
                strncpy(dirPath, path, strlen(path) + 1);
                strncat(dirPath, "/", 2);
                strncat(dirPath, e->name, strlen(e->name) + 1);

                if (strlen(dirPath) > PATH_NAME_MAX_LEN) {
                    DEBUG('f', "Path too long.\n");
                    error = true;
                }

                DEBUG('f', "Checking directory: %s.\n", dirPath);

                Directory *dir = new Directory(0);
                const RawDirectory *rdir = dir->GetRaw();

                // Can't use `dirPath` because FindDirectory() modifies string.
                char *tmp = new char [strlen(dirPath) + 1];
                strncpy(tmp, dirPath, strlen(dirPath) + 1);
                DirSynch *dirSynch = fileSystem->FindDirectory(tmp);
                delete [] tmp;

                dir->FetchFrom(dirSynch->GetFile());
                error = CheckDirectory(rdir, shadowMap, dirPath);
                delete dir;
            }
        }
    }
    return error;
}

bool
FileSystem::Check()
{
    DEBUG('f', "Performing filesystem check\n");
    bool error = false;

    Bitmap *shadowMap = new Bitmap(NUM_SECTORS);
    shadowMap->Mark(FREE_MAP_SECTOR);
    shadowMap->Mark(DIRECTORY_SECTOR);

    DEBUG('f', "Checking bitmap's file header.\n");

    FileHeader *bitH = new FileHeader;
    const RawFileHeader *bitRH = bitH->GetRaw();
    bitH->FetchFrom(FREE_MAP_SECTOR);
    DEBUG('f', "  File size: %u bytes, expected %u bytes.\n"
               "  Number of sectors: %u, expected %u.\n",
          bitRH->numBytes, FREE_MAP_FILE_SIZE,
          bitRH->numSectors, FREE_MAP_FILE_SIZE / SECTOR_SIZE);
    error |= CheckForError(bitRH->numBytes == FREE_MAP_FILE_SIZE,
                           "Bad bitmap header: wrong file size.\n");
    error |= CheckForError(bitRH->numSectors == FREE_MAP_FILE_SIZE / SECTOR_SIZE,
                           "Bad bitmap header: wrong number of sectors.\n");
    error |= CheckFileHeader(bitH, FREE_MAP_SECTOR, shadowMap);
    delete bitH;

    DEBUG('f', "Checking directory: /.\n");

    FileHeader *dirH = new FileHeader;
    dirH->FetchFrom(DIRECTORY_SECTOR);
    error |= CheckFileHeader(dirH, DIRECTORY_SECTOR, shadowMap);
    delete dirH;

    Bitmap *freeMap = new Bitmap(NUM_SECTORS);
    OpenFile *freeMapFile = fsSynchList->Get(FREE_MAP_SECTOR)->GetFile();
    freeMap->FetchFrom(freeMapFile);
    Directory *dir = new Directory(0);
    const RawDirectory *rdir = dir->GetRaw();
    OpenFile *directoryFile = fsSynchList->Get(DIRECTORY_SECTOR)->GetFile();
    dir->FetchFrom(directoryFile);
    error |= CheckDirectory(rdir, shadowMap, "");
    delete dir;

    // The two bitmaps should match.
    DEBUG('f', "Checking bitmap consistency.\n");
    error |= CheckBitmaps(freeMap, shadowMap);
    delete shadowMap;
    delete freeMap;

    DEBUG('f', error ? "Filesystem check failed.\n"
                     : "Filesystem check succeeded.\n");

    return !error;
}

/// Print everything about the file system:
/// * the contents of the bitmap;
/// * the contents of every directory;
/// * for each file in the directory:
///   * the contents of the file header;
///   * the data in the file.
void
FileSystem::Print()
{
    FileHeader *bitH    = new FileHeader;
    FileHeader *dirH    = new FileHeader;
    Bitmap     *freeMap = new Bitmap(NUM_SECTORS);
    Directory  *dir     = new Directory(0);

    printf("--------------------------------\n");
    bitH->FetchFrom(FREE_MAP_SECTOR);
    bitH->Print("Bitmap");

    printf("--------------------------------\n");
    OpenFile *freeMapFile = fsSynchList->Get(FREE_MAP_SECTOR)->GetFile();
    freeMap->FetchFrom(freeMapFile);
    freeMap->Print();

    printf("--------------------------------\n");
    dirH->FetchFrom(DIRECTORY_SECTOR);
    dirH->Print("Root directory");

    printf("--------------------------------\n");
    printf("--- Directory path: /\n");
    OpenFile *directoryFile = fsSynchList->Get(DIRECTORY_SECTOR)->GetFile();
    dir->FetchFrom(directoryFile);
    dir->Print("");
    printf("--------------------------------\n");

    delete bitH;
    delete dirH;
    delete freeMap;
    delete dir;
}
