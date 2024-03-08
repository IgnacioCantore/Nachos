#include "fs_synch.hh"
#include "open_file.hh"
#include "file_header.hh"
#include "threads/synch.hh"

FSSynch::FSSynch(OpenFile *file, int sector)
{
    openFile  = file;
    hdrSector = sector;
    lock      = new Lock("Directory/FreeMap Lock");
}

FSSynch::~FSSynch()
{
    delete lock;
    delete openFile;
}

OpenFile *
FSSynch::GetFile()
{
    return openFile;
}

FileHeader *
FSSynch::GetHeader()
{
    return openFile->GetHeader();
}

int
FSSynch::GetSector()
{
    return hdrSector;
}

void
FSSynch::AcquireLock()
{
    lock->Acquire();
}

void
FSSynch::ReleaseLock()
{
    lock->Release();
}
