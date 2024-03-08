#include "file_synch_list.hh"
#include "threads/synch.hh"

FileSynchList::FileSynchList()
{
    list     = new List<FileSynch *>;
    listLock = new Lock("List Lock");
}

FileSynchList::~FileSynchList()
{
    delete list;
    delete listLock;
}

void
FileSynchList::Add(const char *name, int sector)
{
    ASSERT(listLock->IsHeldByCurrentThread());
    FileSynch *fileSynch = new FileSynch(name, sector);
    list->SortedInsert(fileSynch, sector);
}

FileSynch *
FileSynchList::Get(int sector)
{
    ASSERT(listLock->IsHeldByCurrentThread());
    return list->Get(sector);
}

void
FileSynchList::Remove(FileSynch *fileSynch)
{
    ASSERT(listLock->IsHeldByCurrentThread());
    list->Remove(fileSynch);
}

FileSynch *
FileSynchList::Pop()
{
    return list->Pop();
}

bool
FileSynchList::IsEmpty()
{
    return list->IsEmpty();
}

void
FileSynchList::AcquireLock()
{
    listLock->Acquire();
}

void
FileSynchList::ReleaseLock()
{
    listLock->Release();
}
