/// Routines for managing the disk file header (in UNIX, this would be called
/// the i-node).
///
/// The file header is used to locate where on disk the file's data is
/// stored.  We implement this as a fixed size table of pointers -- each
/// entry in the table points to the disk sector containing that portion of
/// the file data (in other words, there are no indirect or doubly indirect
/// blocks). The table size is chosen so that the file header will be just
/// big enough to fit in one disk sector,
///
/// Unlike in a real system, we do not keep track of file permissions,
/// ownership, last modification date, etc., in the file header.
///
/// A file header can be initialized in two ways:
///
/// * for a new file, by modifying the in-memory data structure to point to
///   the newly allocated data blocks;
/// * for a file already on disk, by reading the file header from disk.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "file_header.hh"
#include "threads/system.hh"

#include <ctype.h>
#include <stdio.h>


/// Initialize a fresh file header for a newly created file.  Allocate data
/// blocks for the file out of the map of free disk blocks.  Return false if
/// there are not enough free blocks to accomodate the new file.
///
/// * `freeMap` is the bit map of free disk sectors.
/// * `fileSize` is the bit map of free disk sectors.
bool
FileHeader::Allocate(Bitmap *freeMap, unsigned fileSize)
{
    ASSERT(freeMap != nullptr);

    raw.numBytes = fileSize;
    raw.numSectors = DivRoundUp(fileSize, SECTOR_SIZE);

    unsigned indirData = 0;
    unsigned indirSectors = 0;

    if (fileSize > MAX_DIRECT_SIZE) {
        indirData = fileSize - MAX_DIRECT_SIZE;
        indirSectors = DivRoundUp(indirData, SECTOR_SIZE);  // For each indir
                                                            // block.
        indirSectors += DivRoundUp(indirSectors, NUM_INDIRECT) + 1;
    }

    if (freeMap->CountClear() < raw.numSectors + indirSectors)
        return false;  // Not enough space.

    unsigned dirSectors = min(raw.numSectors, NUM_DIRECT);

    for (unsigned i = 0; i < dirSectors; i++)
        raw.dataSectors[i] = freeMap->Find();

    if (indirSectors == 0) {
        raw.indirSector = -1;
        for (unsigned i = 0; i < NUM_INDIRECT; i++) {
            firstIndir[i] = -1;
            for (unsigned j = 0; j < NUM_INDIRECT; j++)
                secondIndir[i][j] = -1;
        }
    } else {
        raw.indirSector = freeMap->Find();
        indirSectors--;
        unsigned sectorsLeft = raw.numSectors - NUM_DIRECT;

        for (unsigned i = 0; i < NUM_INDIRECT; i++) {
            if (i < indirSectors) {
                firstIndir[i] = freeMap->Find();
                for (unsigned j = 0; j < NUM_INDIRECT; j++) {
                    if (sectorsLeft > 0) {
                        secondIndir[i][j] = freeMap->Find();
                        sectorsLeft--;
                    } else
                        secondIndir[i][j] = -1;
                }
            } else {
                firstIndir[i] = -1;
                for (unsigned j = 0; j < NUM_INDIRECT; j++)
                    secondIndir[i][j] = -1;
            }
        }
    }
    return true;
}

/// De-allocate all the space allocated for data blocks for this file.
///
/// * `freeMap` is the bit map of free disk sectors.
void
FileHeader::Deallocate(Bitmap *freeMap)
{
    ASSERT(freeMap != nullptr);

    unsigned dirSectors = min(raw.numSectors, NUM_DIRECT);

    for (unsigned i = 0; i < dirSectors; i++) {
        ASSERT(freeMap->Test(raw.dataSectors[i]));  // ought to be marked!
        freeMap->Clear(raw.dataSectors[i]);
    }

    if (raw.indirSector != -1) {
        ASSERT(freeMap->Test(raw.indirSector));  // ought to be marked!
        freeMap->Clear(raw.indirSector);

        for (unsigned i = 0; i < NUM_INDIRECT && firstIndir[i] != -1; i++) {
            ASSERT(freeMap->Test(firstIndir[i]));  // ought to be marked!
            freeMap->Clear(firstIndir[i]);
            for (unsigned j = 0; j < NUM_INDIRECT && secondIndir[i][j] != -1;
                 j++) {
                ASSERT(freeMap->Test(secondIndir[i][j]));  // ought to be
                freeMap->Clear(secondIndir[i][j]);         // marked!
            }
        }
    }
}

/// Fetch contents of file header from disk.
///
/// * `sector` is the disk sector containing the file header.
void
FileHeader::FetchFrom(unsigned sector)
{
    synchDisk->ReadSector(sector, (char *) &raw);
    if (raw.indirSector != -1) {
        synchDisk->ReadSector(raw.indirSector, (char *) firstIndir);
        for (unsigned i = 0; i < NUM_INDIRECT && firstIndir[i] != -1; i++)
            synchDisk->ReadSector(firstIndir[i], (char *) secondIndir[i]);
    }
}

/// Write the modified contents of the file header back to disk.
///
/// * `sector` is the disk sector to contain the file header.
void
FileHeader::WriteBack(unsigned sector)
{
    synchDisk->WriteSector(sector, (char *) &raw);
    if (raw.indirSector != -1) {
        synchDisk->WriteSector(raw.indirSector, (char *) firstIndir);
        for (unsigned i = 0; i < NUM_INDIRECT && firstIndir[i] != -1; i++)
            synchDisk->WriteSector(firstIndir[i], (char *) secondIndir[i]);
    }
}

/// Return which disk sector is storing a particular byte within the file.
/// This is essentially a translation from a virtual address (the offset in
/// the file) to a physical address (the sector where the data at the offset
/// is stored).
///
/// * `offset` is the location within the file of the byte in question.
unsigned
FileHeader::ByteToSector(unsigned offset)
{
    unsigned sectorIndex = offset / SECTOR_SIZE;
    if (sectorIndex < NUM_DIRECT)
        return raw.dataSectors[sectorIndex];

    unsigned indirSectorIndex = sectorIndex - NUM_DIRECT;

    unsigned firstIndirIndex = indirSectorIndex / NUM_INDIRECT;
    unsigned secondIndirIndex = indirSectorIndex % NUM_INDIRECT;

    return secondIndir[firstIndirIndex][secondIndirIndex];
}

/// Return the number of bytes in the file.
unsigned
FileHeader::FileLength() const
{
    return raw.numBytes;
}

/// Print the contents of the file header, and the contents of all the data
/// blocks pointed to by the file header.
void
FileHeader::Print(const char *title)
{
    char *data = new char [SECTOR_SIZE];

    if (title == nullptr)
        printf("File header:\n");
    else
        printf("%s file header:\n", title);

    printf("    size: %u bytes\n"
           "    block indexes: ",
           raw.numBytes);

    unsigned dirSectors = min(raw.numSectors, NUM_DIRECT);

    for (unsigned i = 0; i < dirSectors; i++)
        printf("%u ", raw.dataSectors[i]);

    if (raw.indirSector != -1)
        for (unsigned i = 0; i < NUM_INDIRECT && firstIndir[i] != -1; i++)
            for (unsigned j = 0; j < NUM_INDIRECT && secondIndir[i][j] != -1;
                 j++)
                printf("%u ", secondIndir[i][j]);

    printf("\n");

    for (unsigned i = 0, k = 0; i < dirSectors; i++) {
        printf("    contents of block %u:\n", raw.dataSectors[i]);
        synchDisk->ReadSector(raw.dataSectors[i], data);
        for (unsigned j = 0; j < SECTOR_SIZE && k < raw.numBytes; j++, k++) {
            if (isprint(data[j]))
                printf("%c", data[j]);
            else
                printf("\\%X", (unsigned char) data[j]);
        }
        printf("\n");
    }

    if (raw.indirSector != -1)
    for (unsigned m = 0, k = 0; m < NUM_INDIRECT && firstIndir[m] != -1; m++) {
        for (unsigned n = 0; n < NUM_INDIRECT && secondIndir[m][n] != -1;
             n++) {
            printf("    contents of block %u:\n", secondIndir[m][n]);
            synchDisk->ReadSector(secondIndir[m][n], data);
            for (unsigned j = 0; j < SECTOR_SIZE && k < raw.numBytes;
                 j++, k++) {
                if (isprint(data[j]))
                    printf("%c", data[j]);
                else
                    printf("\\%X", (unsigned char) data[j]);
            }
            printf("\n");
        }
    }
    delete [] data;
}

/// Expand a file by allocating new sectors to hold more data.
/// * `freeMap` is the bit map of free disk sectors.
/// * `numBytes` indicates how many bytes the file is being increased.
bool
FileHeader::Expand(Bitmap *freeMap, unsigned newBytes)
{
    ASSERT(newBytes != 0);
    ASSERT(freeMap != nullptr);

    unsigned onLastSector = (SECTOR_SIZE - (raw.numBytes % SECTOR_SIZE)) %
                            SECTOR_SIZE;  // Free space on last used sector
    unsigned remainingData = newBytes > onLastSector ? newBytes -
                                                       onLastSector : 0;
    unsigned newSectors = DivRoundUp(remainingData, SECTOR_SIZE);
    unsigned indirSectors = 0;

    if (raw.indirSector != -1) {
        unsigned onLastIndir = (raw.numSectors - NUM_DIRECT) % NUM_INDIRECT;
        unsigned remainingSectors = newSectors > onLastIndir ? newSectors -
                                                               onLastIndir : 0;
        indirSectors = DivRoundUp(remainingSectors, NUM_INDIRECT);
    } else if (raw.numBytes + newBytes > MAX_DIRECT_SIZE) {
        unsigned onDirSectors = NUM_DIRECT - raw.numSectors;
        indirSectors = DivRoundUp(newSectors - onDirSectors, NUM_INDIRECT) + 1;
    }

    if (freeMap->CountClear() < newSectors + indirSectors)
      return false;  // Not enough space.

    DEBUG('f', "Expanding file of length %u to %u.\n",
          raw.numBytes, raw.numBytes + newBytes);

    unsigned oldSectors = raw.numSectors;

    raw.numBytes += newBytes;
    raw.numSectors += newSectors;

    if (oldSectors < NUM_DIRECT)
        for (unsigned i = oldSectors; i < min(raw.numSectors, NUM_DIRECT);
             i++) {
            raw.dataSectors[i] = freeMap->Find();
            newSectors--;
        }

    if (raw.numSectors > NUM_DIRECT) {
        if (raw.indirSector == -1) {
            raw.indirSector = freeMap->Find();
            indirSectors--;
        }

        if (indirSectors > 0) {
            for (unsigned i = 0; i < NUM_INDIRECT && indirSectors > 0; i++) {
                if (firstIndir[i] == -1) {
                    firstIndir[i] = freeMap->Find();
                    indirSectors--;
                }
                for (unsigned j = 0; j < NUM_INDIRECT && newSectors > 0; j++)
                    if (secondIndir[i][j] == -1) {
                        secondIndir[i][j] = freeMap->Find();
                        newSectors--;
                    }
            }
        }
    }
    return true;
}

const RawFileHeader *
FileHeader::GetRaw() const
{
    return &raw;
}
