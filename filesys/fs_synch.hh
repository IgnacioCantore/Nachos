#ifndef NACHOS_FILESYS_DIRECTORYSYNCH__HH
#define NACHOS_FILESYS_DIRECTORYSYNCH__HH

class Lock;
class OpenFile;
class FileHeader;

class FSSynch {
public:

    FSSynch(OpenFile *file, int sector);

    ~FSSynch();

    OpenFile *GetFile();

    FileHeader *GetHeader();

    int GetSector();

    void AcquireLock();

    void ReleaseLock();

private:
    OpenFile *openFile;  ///< Directory or bit map of free disk blocks,
                         ///< represented as a file.
    int hdrSector;  ///< Disk sector where the directory/free map's file header
                    ///< is stored.
    Lock *lock;  ///< Lock to ensure mutual exclusion when we are
                 ///< modifying the directory/free map.
};

typedef FSSynch DirSynch;  // For directories synchronization.
typedef FSSynch FreeMapSynch; // For bit map of free disk blocks
                              // synchronization.

#endif
