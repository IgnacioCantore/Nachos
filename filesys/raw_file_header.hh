/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.

#ifndef NACHOS_FILESYS_RAWFILEHEADER__HH
#define NACHOS_FILESYS_RAWFILEHEADER__HH


#include "machine/disk.hh"


static const unsigned NUM_DIRECT
  = (SECTOR_SIZE - 3 * sizeof (int)) / sizeof (int);
const unsigned MAX_DIRECT_SIZE = NUM_DIRECT * SECTOR_SIZE;
static const unsigned NUM_INDIRECT = SECTOR_SIZE / sizeof (int);
const unsigned MAX_INDIR_SIZE = NUM_INDIRECT * SECTOR_SIZE;

struct RawFileHeader {
    unsigned numBytes;  ///< Number of bytes in the file.
    unsigned numSectors;  ///< Number of data sectors in the file.
    int indirSector;  ///< Disk sector number for the first indirection table,
                      ///< -1 if there is no indirection for this file.
    unsigned dataSectors[NUM_DIRECT];  ///< Disk sector numbers for each data
                                       ///< block in the file.
};


#endif
