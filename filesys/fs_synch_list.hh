#ifndef NACHOS_FILESYS_DIRECTORYSYNCHLIST__HH
#define NACHOS_FILESYS_DIRECTORYSYNCHLIST__HH

#include "fs_synch.hh"
#include "lib/list.hh"

class Lock;

class FSSynchList {
public:

    FSSynchList();

    ~FSSynchList();

    FSSynch *Add(OpenFile *file, int sector);

    FSSynch *Get(int sector);

    void Remove(FSSynch *fileSysSynch);

    FSSynch *Pop();

    bool IsEmpty();

    void AcquireLock();

    void ReleaseLock();

private:
    List<FSSynch *> *list;
    Lock *listLock;
};

#endif
