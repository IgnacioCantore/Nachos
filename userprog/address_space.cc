/// Routines to manage address spaces (memory for executing user programs).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "address_space.hh"
#include "executable.hh"
#include "threads/system.hh"
#include "lib/utility.hh"

#include <string.h>
#include <stdio.h>


/// First, set up the translation from program memory to physical memory.
/// For now, this is really simple (1:1), since we are only uniprogramming,
/// and we have a single unsegmented page table.
AddressSpace::AddressSpace(OpenFile *executable_file, SpaceId spaceId)
{
    ASSERT(executable_file != nullptr);

    Executable exe (executable_file);
    ASSERT(exe.CheckMagic());

    // How big is address space?

    unsigned size = exe.GetSize() + USER_STACK_SIZE;
      // We need to increase the size to leave room for the stack.
    numPages = DivRoundUp(size, PAGE_SIZE);
    size = numPages * PAGE_SIZE;

#ifndef VMEM
    ASSERT(numPages <= freePagesMap->CountClear());
      // Check we are not trying to run anything too big -- at least until we
      // have virtual memory.
#endif

    DEBUG('a', "Initializing address space, num pages %u, size %u\n",
          numPages, size);

    // First, set up the translation.

    pageTable = new TranslationEntry[numPages];
    for (unsigned i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
          // For now, virtual page number = physical page number.
#ifndef USE_TLB
        pageTable[i].physicalPage = freePagesMap->Find();
#else
        pageTable[i].physicalPage = -1;
#endif
        pageTable[i].valid        = true;
        pageTable[i].use          = false;
        pageTable[i].dirty        = false;
        pageTable[i].readOnly     = false;
#ifdef VMEM
        pageTable[i].swap         = false;
#endif
          // If the code segment was entirely on a separate page, we could
          // set its pages to be read-only.
    }

#ifdef VMEM
    char asid[3];
    sprintf(asid, "%d", spaceId);
    char swapFilename[SWAP_PATH_MAX_LEN + 1];

#ifdef FILESYS
    strncpy(swapFilename, "/SWAP.", 7);
#else
    strncpy(swapFilename, "SWAP.", 6);
#endif
    strncat(swapFilename, asid, 3);

    fileSystem->Create(swapFilename, size);
    swapFile = fileSystem->Open(swapFilename);
#endif

#ifdef USE_TLB
    execFile = executable_file;

    codeSize = exe.GetCodeSize();
    initDataSize = exe.GetInitDataSize();

    codeAddr = exe.GetCodeAddr();
    initDataAddr = exe.GetInitDataAddr();

#else
    char *mainMemory = machine->GetMMU()->mainMemory;

    // Zero out the entire address space, to zero the unitialized data
    // segment and the stack segment.
    for (unsigned i = 0; i < numPages; i++)
        memset(&mainMemory[pageTable[i].physicalPage * PAGE_SIZE], 0,
               PAGE_SIZE);

    // Then, copy in the code and data segments into memory.
    uint32_t codeSize = exe.GetCodeSize();
    uint32_t initDataSize = exe.GetInitDataSize();

    if (codeSize > 0) {
        uint32_t virtualAddr = exe.GetCodeAddr();
        DEBUG('a', "Initializing code segment, at 0x%X, size %u\n",
              virtualAddr, codeSize);
        uint32_t virtualPage     = virtualAddr / PAGE_SIZE;
        uint32_t offset          = virtualAddr % PAGE_SIZE;
        uint32_t currentCodeSize = codeSize;
        uint32_t sizeToRead      = min(PAGE_SIZE - offset, currentCodeSize);

        exe.ReadCodeBlock(&mainMemory[(pageTable[virtualPage].physicalPage *
                          PAGE_SIZE) + offset], sizeToRead, 0);
        currentCodeSize -= sizeToRead;
        virtualPage++;

        for (; currentCodeSize > 0; virtualPage++) {
            sizeToRead = min(PAGE_SIZE, currentCodeSize);
            exe.ReadCodeBlock(&mainMemory[pageTable[virtualPage].physicalPage *
                              PAGE_SIZE], sizeToRead,
                              codeSize - currentCodeSize);
            currentCodeSize -= sizeToRead;
        }
    }

    if (initDataSize > 0) {
        uint32_t virtualAddr = exe.GetInitDataAddr();
        DEBUG('a', "Initializing data segment, at 0x%X, size %u\n",
              virtualAddr, initDataSize);
        uint32_t virtualPage      = virtualAddr / PAGE_SIZE;
        uint32_t offset           = virtualAddr % PAGE_SIZE;
        uint32_t currentIDataSize = initDataSize;
        uint32_t sizeToRead       = min(PAGE_SIZE - offset, currentIDataSize);

        exe.ReadDataBlock(&mainMemory[(pageTable[virtualPage].physicalPage *
                          PAGE_SIZE) + offset], sizeToRead, 0);
        currentIDataSize -= sizeToRead;
        virtualPage++;

        for (; currentIDataSize > 0; virtualPage++) {
            sizeToRead = min(PAGE_SIZE, currentIDataSize);
            exe.ReadDataBlock(&mainMemory[pageTable[virtualPage].physicalPage *
                              PAGE_SIZE], sizeToRead,
                              initDataSize - currentIDataSize);
            currentIDataSize -= sizeToRead;
        }
    }
#endif
}

/// Deallocate an address space.
AddressSpace::~AddressSpace()
{
    for (unsigned i = 0; i < numPages; i++)
        if (pageTable[i].physicalPage != -1)
#ifdef VMEM
            if (coremap->InMemory(this, pageTable[i]))
#endif
                freePagesMap->Clear(pageTable[i].physicalPage);

    delete [] pageTable;

#ifdef USE_TLB
    delete execFile;
#endif

#ifdef VMEM
    delete swapFile;
#endif
}

/// Set the initial values for the user-level register set.
///
/// We write these directly into the “machine” registers, so that we can
/// immediately jump to user code.  Note that these will be saved/restored
/// into the `currentThread->userRegisters` when this thread is context
/// switched out.
void
AddressSpace::InitRegisters()
{
    for (unsigned i = 0; i < NUM_TOTAL_REGS; i++)
        machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of `Start`.
    machine->WriteRegister(PC_REG, 0);

    // Need to also tell MIPS where next instruction is, because of branch
    // delay possibility.
    machine->WriteRegister(NEXT_PC_REG, 4);

    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we do not
    // accidentally reference off the end!
    machine->WriteRegister(STACK_REG, numPages * PAGE_SIZE - 16);
    DEBUG('a', "Initializing stack register to %u\n",
          numPages * PAGE_SIZE - 16);
}

/// On a context switch, save any machine state, specific to this address
/// space, that needs saving.
void
AddressSpace::SaveState()
{
#ifdef VMEM
    TranslationEntry *tlb = machine->GetMMU()->tlb;

    for (unsigned i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].valid && tlb[i].dirty) {
            unsigned vpn = tlb[i].virtualPage;
            pageTable[vpn].dirty = true;
        }
    }
#endif
}

/// On a context switch, restore the machine state so that this address space
/// can run.
void
AddressSpace::RestoreState()
{
#ifndef USE_TLB
    machine->GetMMU()->pageTable     = pageTable;
    machine->GetMMU()->pageTableSize = numPages;
#else
    TranslationEntry *tlb = machine->GetMMU()->tlb;

    for (unsigned i = 0; i < TLB_SIZE; i++)
        tlb[i].valid = false;
#endif
}

#ifdef USE_TLB

TranslationEntry
AddressSpace::LoadPage(unsigned vpn)
{
    pageTable[vpn].valid = true;
    pageTable[vpn].use   = true;
    if (!coremap->InMemory(this, pageTable[vpn])) {
        if (freePagesMap->CountClear() == 0)
            coremap->FreePage();
        char *mainMemory = machine->GetMMU()->mainMemory;
        // Load from binary
        if (pageTable[vpn].physicalPage == -1 || !pageTable[vpn].swap) {
            Executable exe (execFile);
            int physPage = coremap->Find(this, vpn);
            pageTable[vpn].physicalPage = physPage;

            if (vpn * PAGE_SIZE > codeSize + initDataSize)
                memset(&mainMemory[physPage * PAGE_SIZE], 0, PAGE_SIZE);
            else {
                unsigned virtualPageStart = vpn * PAGE_SIZE;
                unsigned virtualPageEnd = virtualPageStart + PAGE_SIZE;

                unsigned offset = max(virtualPageStart, codeAddr);
                unsigned end = min(virtualPageEnd, codeSize + codeAddr);

                if (offset < end)
                    exe.ReadCodeBlock(&mainMemory[physPage * PAGE_SIZE +
                                                  offset - virtualPageStart],
                                      end - offset, offset - codeAddr);

                offset = max(virtualPageStart, initDataAddr);
                end = min(virtualPageEnd, initDataSize + initDataAddr);

                if (offset < end)
                    exe.ReadDataBlock(&mainMemory[physPage * PAGE_SIZE +
                                                  offset - virtualPageStart],
                                      end - offset, offset - initDataAddr);
            }
        } else {  // Load from swap
            int physPage = coremap->Find(this, vpn);
            pageTable[vpn].physicalPage = physPage;
            swapFile->ReadAt(&mainMemory[physPage * PAGE_SIZE], PAGE_SIZE,
                             vpn * PAGE_SIZE);
        }
    }
    return pageTable[vpn];
}
#endif

#ifdef VMEM

TranslationEntry *
AddressSpace::GetPage(unsigned vpn)
{
    return &pageTable[vpn];
}

void
AddressSpace::SaveToSwap(unsigned vpn)
{
    if (pageTable[vpn].dirty) {
        pageTable[vpn].swap = true;
        char *mainMemory = machine->GetMMU()->mainMemory;
        swapFile->WriteAt(&mainMemory[pageTable[vpn].physicalPage * PAGE_SIZE],
                          PAGE_SIZE, vpn * PAGE_SIZE);
    }
    pageTable[vpn].valid = false;
    pageTable[vpn].use   = false;
    pageTable[vpn].dirty = false;

    TranslationEntry *tlb = machine->GetMMU()->tlb;

    for (unsigned i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].physicalPage == pageTable[vpn].physicalPage) {
            tlb[i].valid = false;
            break;
        }
    }
}

#endif
