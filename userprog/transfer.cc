/// Copyright (c) 2019-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "transfer.hh"
#include "lib/utility.hh"
#include "threads/system.hh"

#ifdef USE_TLB
static const int MAX_PAGE_FAULTS = 3;
#else
static const int MAX_PAGE_FAULTS = 1;
#endif


void ReadBufferFromUser(int userAddress, char *outBuffer,
                        unsigned byteCount)
{
    ASSERT(userAddress != 0);
    ASSERT(outBuffer != nullptr);
    ASSERT(byteCount != 0);

    unsigned count = 0;
    do {
        int temp;
        count++;
        for (int i = 0; !(machine->ReadMem(userAddress, 1, &temp)) &&
             i < MAX_PAGE_FAULTS; i++);
        userAddress++;
        *outBuffer = (unsigned char) temp;
        outBuffer++;
    } while (count < byteCount);
}

bool ReadStringFromUser(int userAddress, char *outString,
                        unsigned maxByteCount)
{
    ASSERT(userAddress != 0);
    ASSERT(outString != nullptr);
    ASSERT(maxByteCount != 0);

    unsigned count = 0;
    do {
        int temp;
        count++;
        for (int i = 0; !(machine->ReadMem(userAddress, 1, &temp)) &&
             i < MAX_PAGE_FAULTS; i++);
        userAddress++;
        *outString = (unsigned char) temp;
    } while (*outString++ != '\0' && count < maxByteCount);

    return *(outString - 1) == '\0';
}

void WriteBufferToUser(const char *buffer, int userAddress,
                       unsigned byteCount)
{
    ASSERT(buffer != nullptr);
    ASSERT(userAddress != 0);
    ASSERT(byteCount != 0);

    unsigned count = 0;
    do {
        int temp = *buffer;
        count++;
        for (int i = 0; !(machine->WriteMem(userAddress, 1, temp)) &&
             i < MAX_PAGE_FAULTS; i++);
        userAddress++;
        buffer++;
    } while (count < byteCount);
}

void WriteStringToUser(const char *string, int userAddress)
{
    ASSERT(string != nullptr);
    ASSERT(userAddress != 0);

    do {
        int temp = *string;
        for (int i = 0; !(machine->WriteMem(userAddress, 1, temp)) &&
             i < MAX_PAGE_FAULTS; i++);
        userAddress++;
    } while (*string++ != '\0');
}
