#include "fs_synch_list.hh"
#include "threads/synch.hh"

FSSynchList::FSSynchList()
{
    list     = new List<FSSynch *>;
    listLock = new Lock("List Lock");
}

FSSynchList::~FSSynchList()
{
    delete list;
    delete listLock;
}

FSSynch *
FSSynchList::Add(OpenFile *file, int sector)
{
    ASSERT(listLock->IsHeldByCurrentThread());
    FSSynch *fileSysSynch = new FSSynch(file, sector);
    list->SortedInsert(fileSysSynch, sector);
    return fileSysSynch;
}

FSSynch *
FSSynchList::Get(int sector)
{
    if (sector != FREE_MAP_SECTOR && sector != DIRECTORY_SECTOR)
      // We always have these on the list.
      ASSERT(listLock->IsHeldByCurrentThread());
    return list->Get(sector);
}

void
FSSynchList::Remove(FSSynch *fileSysSynch)
{
    ASSERT(listLock->IsHeldByCurrentThread());
    list->Remove(fileSysSynch);
}

FSSynch *
FSSynchList::Pop()
{
    return list->Pop();
}

bool
FSSynchList::IsEmpty()
{
    return list->IsEmpty();
}

void
FSSynchList::AcquireLock()
{
    listLock->Acquire();
}

void
FSSynchList::ReleaseLock()
{
    listLock->Release();
}
