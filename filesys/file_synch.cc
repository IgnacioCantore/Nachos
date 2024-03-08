#include "file_synch.hh"
#include "file_header.hh"
#include "threads/synch.hh"

#include <string.h>

FileSynch::FileSynch(const char *path, unsigned hdrSector)
{
    strncpy(filepath, path, PATH_NAME_MAX_LEN);
    opened         = 1;
    beingRemoved   = false;
    reading        = 0;
    writing        = false;
    waitingToWrite = 0;

    fileLock       = new Lock("File Lock");
    fileCond       = new Condition("Read/Write Condition", fileLock);

    hdr            = new FileHeader;
    hdr->FetchFrom(hdrSector);
}

FileSynch::~FileSynch()
{
    delete fileCond;
    delete fileLock;
    delete hdr;
}

const char *
FileSynch::GetFilePath() const
{
    return filepath;
}

FileHeader *
FileSynch::GetFileHeader()
{
    return hdr;
}

bool
FileSynch::FileOpened()
{
    fileLock->Acquire();
    bool removing = beingRemoved;
    if (!removing)
        opened++;
    fileLock->Release();
    return removing;
}

bool
FileSynch::FileClosed()
{
    fileLock->Acquire();
    opened--;
    bool allClosed = opened == 0;
    fileLock->Release();
    return allClosed;
}

void
FileSynch::SetToRemove()
{
    fileLock->Acquire();
    beingRemoved = true;
    fileLock->Release();
}

bool
FileSynch::ReadyToRemove()
{
    fileLock->Acquire();
    bool ready = beingRemoved && opened == 0;
    fileLock->Release();
    return ready;
}

void
FileSynch::BeginReading()
{
    fileLock->Acquire();
    while (writing || waitingToWrite > 0)
        fileCond->Wait();
    reading++;
    fileLock->Release();
}

void
FileSynch::FinishReading()
{
    fileLock->Acquire();
    reading--;
    if (reading == 0)
        fileCond->Broadcast();
    fileLock->Release();
}

void
FileSynch::BeginWriting()
{
    fileLock->Acquire();
    waitingToWrite++;
    while (writing || reading > 0)
        fileCond->Wait();
    waitingToWrite--;
    writing = true;
    fileLock->Release();
}

void
FileSynch::FinishWriting()
{
    fileLock->Acquire();
    writing = false;
    fileCond->Broadcast();
    fileLock->Release();
}
