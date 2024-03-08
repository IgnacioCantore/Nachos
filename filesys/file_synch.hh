#ifndef NACHOS_FILESYS_FILESYNCH__HH
#define NACHOS_FILESYS_FILESYNCH__HH

#include "directory_entry.hh"

class FileHeader;
class Lock;
class Condition;

class FileSynch {
public:

    FileSynch(const char *name, unsigned hdrSector);

    ~FileSynch();

    const char *GetFilePath() const;

    FileHeader *GetFileHeader();

    bool FileOpened();

    bool FileClosed();

    void SetToRemove();

    bool ReadyToRemove();

    void BeginReading();

    void FinishReading();

    void BeginWriting();

    void FinishWriting();

private:
    char filepath[PATH_NAME_MAX_LEN + 1];
    FileHeader *hdr;

    unsigned opened;
    bool beingRemoved;
    unsigned reading;
    bool writing;
    unsigned waitingToWrite;

    Lock *fileLock;
    Condition *fileCond;
};

#endif
