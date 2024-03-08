#ifndef NACHOS_FILESYS_FILESYNCHLIST__HH
#define NACHOS_FILESYS_FILESYNCHLIST__HH

#include "file_synch.hh"
#include "lib/list.hh"

class Lock;

class FileSynchList {
public:

    FileSynchList();

    ~FileSynchList();

    void Add(const char *name, int sector);

    FileSynch *Get(int sector);

    void Remove(FileSynch *fileSynch);

    FileSynch *Pop();

    bool IsEmpty();

    void AcquireLock();

    void ReleaseLock();

private:
    List<FileSynch *> *list;
    Lock *listLock;
};

#endif
