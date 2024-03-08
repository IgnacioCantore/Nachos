/// Entry points into the Nachos kernel from user programs.
///
/// There are two kinds of things that can cause control to transfer back to
/// here from user code:
///
/// * System calls: the user code explicitly requests to call a procedure in
///   the Nachos kernel.  Right now, the only function we support is `Halt`.
///
/// * Exceptions: the user code does something that the CPU cannot handle.
///   For instance, accessing memory that does not exist, arithmetic errors,
///   etc.
///
/// Interrupts (which can also cause control to transfer from user code into
/// the Nachos kernel) are handled elsewhere.
///
/// For now, this only handles the `Halt` system call.  Everything else core-
/// dumps.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "transfer.hh"
#include "syscall.h"
#include "filesys/directory_entry.hh"
#include "threads/system.hh"
#include "args.hh"

#include <stdio.h>
#include <string.h>


static void
IncrementPC()
{
    unsigned pc;

    pc = machine->ReadRegister(PC_REG);
    machine->WriteRegister(PREV_PC_REG, pc);
    pc = machine->ReadRegister(NEXT_PC_REG);
    machine->WriteRegister(PC_REG, pc);
    pc += 4;
    machine->WriteRegister(NEXT_PC_REG, pc);
}

/// Do some default behavior for an unexpected exception.
///
/// NOTE: this function is meant specifically for unexpected exceptions.  If
/// you implement a new behavior for some exception, do not extend this
/// function: assign a new handler instead.
///
/// * `et` is the kind of exception.  The list of possible exceptions is in
///   `machine/exception_type.hh`.
static void
DefaultHandler(ExceptionType et)
{
    int exceptionArg = machine->ReadRegister(2);

    fprintf(stderr, "Unexpected user mode exception: %s, arg %d.\n",
            ExceptionTypeToString(et), exceptionArg);
    ASSERT(false);
}

void
RunUserProgram(void *_args)
{
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();

    if (_args != nullptr) {
        char **args = (char **) _args;

        int argc = WriteArgs(args);
        int argvAddr = machine->ReadRegister(STACK_REG) + 16;

        machine->WriteRegister(4, argc);
        machine->WriteRegister(5, argvAddr);
    }

    machine->Run();
}

/// Handle a system call exception.
///
/// * `et` is the kind of exception.  The list of possible exceptions is in
///   `machine/exception_type.hh`.
///
/// The calling convention is the following:
///
/// * system call identifier in `r2`;
/// * 1st argument in `r4`;
/// * 2nd argument in `r5`;
/// * 3rd argument in `r6`;
/// * 4th argument in `r7`;
/// * the result of the system call, if any, must be put back into `r2`.
///
/// And do not forget to increment the program counter before returning. (Or
/// else you will loop making the same system call forever!)
static void
SyscallHandler(ExceptionType _et)
{
    int scid = machine->ReadRegister(2);

    switch (scid) {

        case SC_HALT:
            DEBUG('e', "Shutdown, initiated by user program.\n");
            interrupt->Halt();
            break;

        case SC_CREATE: {
            int filenameAddr = machine->ReadRegister(4);
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filepath[PATH_NAME_MAX_LEN + 1];

            if (!ReadStringFromUser(filenameAddr, filepath,
                sizeof (filepath))) {
                DEBUG('e', "Error: filename string too long "
                           "(maximum is %u bytes).\n",
                      PATH_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Create` requested for file `%s`.\n", filepath);

            if (fileSystem->Create(filepath, 0))
                machine->WriteRegister(2, 0);
            else {
                DEBUG('e', "Error: could not create file `%s`.\n", filepath);
                machine->WriteRegister(2, -1);
            }
            break;
        }

        case SC_REMOVE: {
            int filenameAddr = machine->ReadRegister(4);
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filepath[PATH_NAME_MAX_LEN + 1];

            if (!ReadStringFromUser(filenameAddr, filepath,
                sizeof (filepath))) {
                DEBUG('e', "Error: filename string too long "
                           "(maximum is %u bytes).\n",
                      PATH_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Remove` requested for file %s.\n", filepath);

            if (fileSystem->Remove(filepath))
                machine->WriteRegister(2, 0);
            else {
                DEBUG('e', "Error: could not remove file `%s`.\n", filepath);
                machine->WriteRegister(2, -1);
            }
            break;
        }

        case SC_OPEN: {
            int filenameAddr = machine->ReadRegister(4);
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filepath[PATH_NAME_MAX_LEN + 1];

            if (!ReadStringFromUser(filenameAddr, filepath,
                sizeof (filepath))) {
                DEBUG('e', "Error: filename string too long "
                           "(maximum is %u bytes).\n",
                      PATH_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Open` requested for file `%s`.\n", filepath);

            OpenFile *file = fileSystem->Open(filepath);
            if (file != nullptr) {
                OpenFileId fid = currentThread->AddFile(file);
                if (fid != -1)
                    machine->WriteRegister(2, fid);
                else {
                    DEBUG('e', "Error: file descriptors table is full.\n");
                    delete file;
                    machine->WriteRegister(2, -1);
                }
            } else {
                DEBUG('e', "Error: could not open file `%s`.\n", filepath);
                machine->WriteRegister(2, -1);
            }
            break;
        }

        case SC_CLOSE: {
            int fid = machine->ReadRegister(4);

            DEBUG('e', "`Close` requested for id %u.\n", fid);

            if (fid < 2) {
                DEBUG('e', "Error: file id must be greater than or equal to "
                           "2.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            OpenFile *file = currentThread->RemoveFile(fid);
            if (file != nullptr) {
                delete file;
                machine->WriteRegister(2, 0);
            } else {
                DEBUG('e', "Error: could not close file with id %u.\n", fid);
                machine->WriteRegister(2, -1);
            }
            break;
        }

        case SC_READ: {
            int userString = machine->ReadRegister(4);
            if (userString == 0) {
                DEBUG('e', "Error: address to user string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            int size = machine->ReadRegister(5);
            if (size <= 0) {
                DEBUG('e', "Error: size for Read must be greater than 0.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            OpenFileId fid = machine->ReadRegister (6);
            if (fid < 0) {
                DEBUG('e', "Error: file id must be greater than or equal to "
                           "0.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char tempString[size + 1];
            int bytesRead = 0;

            if (fid == CONSOLE_INPUT) {
                DEBUG('e', "`Read` requested from console input.\n");
                bytesRead = synchConsole->ReadBuffer(tempString, size);
            } else {
                DEBUG('e', "`Read` requested from file with id %u.\n", fid);
                OpenFile *file = currentThread->GetFile(fid);
                if (file != nullptr) {
                    bytesRead = file->Read(tempString, size);
                    tempString[bytesRead] = '\0';
                } else {
                    DEBUG('e', "Error: could not open file with id %u for "
                               "reading.\n", fid);
                    machine->WriteRegister(2, -1);
                    break;
                }
            }
            WriteStringToUser(tempString, userString);
            machine->WriteRegister(2, bytesRead);
            break;
        }

        case SC_WRITE: {
            int userString = machine->ReadRegister(4);
            if (userString == 0) {
                DEBUG('e', "Error: address to user string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            int size = machine->ReadRegister(5);
            if (size <= 0) {
                DEBUG('e', "Error: size for Write must be greater than 0.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            OpenFileId fid = machine->ReadRegister (6);
            if (fid < 0) {
                DEBUG('e', "Error: file id must be greater than or equal to "
                           "0.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char tempString[size + 1];
            ReadBufferFromUser(userString, tempString, size);
            tempString[size] = '\0';
            int bytesWritten = 0;

            if (fid == CONSOLE_OUTPUT) {
                DEBUG('e', "`Write` requested to console output.\n");
                synchConsole->WriteBuffer(tempString, size);
                bytesWritten = size;
            } else {
                DEBUG('e', "`Write` requested to file with id %u.\n", fid);
                OpenFile *file = currentThread->GetFile(fid);
                if (file != nullptr)
                    bytesWritten = file->Write(tempString, size);
                else {
                    DEBUG('e', "Error: could not open file with id %u for "
                               "writting.\n", fid);
                    machine->WriteRegister(2, -1);
                    break;
                }
            }

            if (bytesWritten == size)
                machine->WriteRegister(2, 0);
            else
                machine->WriteRegister(2, -1);
            break;
        }

        case SC_EXEC: {
            int filenameAddr = machine->ReadRegister(4);
            int canJoin = machine->ReadRegister(5);
            int argvAddr = machine->ReadRegister(6);

            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char *filepath = new char [PATH_NAME_MAX_LEN + 1];

            if (!ReadStringFromUser(filenameAddr, filepath,
                                    PATH_NAME_MAX_LEN + 1)) {
                DEBUG('e', "Error: filename string too long "
                           "(maximum is %u bytes).\n", PATH_NAME_MAX_LEN);
                delete [] filepath;
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Exec` requested for file `%s`.\n", filepath);

            OpenFile *executable = fileSystem->Open(filepath);
            if (executable == nullptr) {
                DEBUG('e', "Error: could not open file `%s`.\n", filepath);
                delete [] filepath;
                machine->WriteRegister(2, -1);
                break;
            }

            Thread *newThread = new Thread(filepath, canJoin,
                                           currentThread->GetPriority());
#ifdef FILESYS
            newThread->SetCurrentDir(currentThread->GetCurrentDir());
#endif
            SpaceId spaceId = userThreads->Add(newThread);
            if (spaceId != -1) {
                AddressSpace *space = new AddressSpace(executable, spaceId);
                newThread->space = space;
                newThread->spaceId = spaceId;
#ifndef USE_TLB
                delete executable;
#endif
                if (argvAddr == 0)
                    newThread->Fork(RunUserProgram, nullptr);
                else
                    newThread->Fork(RunUserProgram, SaveArgs(argvAddr));

                machine->WriteRegister(2, spaceId);
            } else {
                DEBUG('e', "Error: user threads table is full.\n");
                delete executable;
                delete newThread;
                delete [] filepath;
                machine->WriteRegister(2, -1);
            }
            break;
        }

        case SC_EXIT: {
            int exitStatus = machine->ReadRegister(4);

            DEBUG('e', "`Exit` requested by thread `%s` with exit status "
                       "%d.\n", currentThread->GetName(), exitStatus);
            currentThread->Finish(exitStatus);
            break;
        }

        case SC_JOIN: {
            SpaceId spaceId = machine->ReadRegister(4);

            DEBUG('e', "`Join` requested for thread with id %d.\n", spaceId);

            if (spaceId < 0) {
                DEBUG('e', "Error: space id must be greater than 0.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            Thread *thread = userThreads->Get(spaceId);
            if (thread != nullptr) {
                int exitStatus = thread->Join();
                machine->WriteRegister(2, exitStatus);
            } else {
                DEBUG('e', "Error: could not find thread with id %d for "
                           "joining.\n", spaceId);
                machine->WriteRegister(2, -1);
            }
            break;
        }

#ifdef FILESYS
        case SC_MKDIR: {
            int dirPathAddr = machine->ReadRegister(4);
            if (dirPathAddr == 0) {
                DEBUG('e', "Error: address to directory name string is "
                           "null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char dirPath[PATH_NAME_MAX_LEN + 1];
            if (!ReadStringFromUser(dirPathAddr, dirPath, sizeof (dirPath))) {
                DEBUG('e', "Error: directory name string too long "
                           "(maximum is %u bytes).\n", PATH_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Mkdir` requested for directory `%s`.\n", dirPath);

            if (fileSystem->Create(dirPath, 0, true))
                machine->WriteRegister(2, 0);
            else {
                DEBUG('e', "Error: could not create directory `%s`.\n",
                      dirPath);
                machine->WriteRegister(2, -1);
            }
            break;
        }

        case SC_CD: {
            int dirPathAddr = machine->ReadRegister(4);
            if (dirPathAddr == 0) {
                DEBUG('e', "Error: address to directory name string is "
                           "null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char dirPath[PATH_NAME_MAX_LEN + 1];

            if (!ReadStringFromUser(dirPathAddr, dirPath, sizeof (dirPath))) {
                DEBUG('e', "Error: directory name string too long "
                           "(maximum is %u bytes).\n", PATH_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Cd` requested for directory `%s`.\n", dirPath);

            DirSynch *newDir = fileSystem->FindDirectory(dirPath);
            if (newDir != nullptr) {
                currentThread->SetCurrentDir(newDir);
                machine->WriteRegister(2, 0);
            } else {
                DEBUG('e', "Error: could not change to directory `%s`.\n",
                      dirPath);
                machine->WriteRegister(2, -1);
            }
            break;
        }
#endif
        default:
            fprintf(stderr, "Unexpected system call: id %d.\n", scid);
            ASSERT(false);
    }

    IncrementPC();
}

#ifdef USE_TLB
static void
PageFaultHandler(ExceptionType _et)
{
    static unsigned TLB_ENTRY = 0;

    unsigned vaddr = machine->ReadRegister(BAD_VADDR_REG);
    unsigned vpn = vaddr / PAGE_SIZE;

    TranslationEntry *tlb = machine->GetMMU()->tlb;

    if (tlb[TLB_ENTRY].valid && tlb[TLB_ENTRY].dirty)
        coremap->UpdateEntry(tlb[TLB_ENTRY].physicalPage);

    tlb[TLB_ENTRY] = currentThread->space->LoadPage(vpn);
    TLB_ENTRY = (TLB_ENTRY + 1) % TLB_SIZE;
}

static void
ReadOnlyHandler(ExceptionType _et)
{
    currentThread->Finish(-1);
}
#endif

/// By default, only system calls have their own handler.  All other
/// exception types are assigned the default handler.
void
SetExceptionHandlers()
{
    machine->SetHandler(NO_EXCEPTION,            &DefaultHandler);
    machine->SetHandler(SYSCALL_EXCEPTION,       &SyscallHandler);
#ifdef USE_TLB
    machine->SetHandler(PAGE_FAULT_EXCEPTION,    &PageFaultHandler);
    machine->SetHandler(READ_ONLY_EXCEPTION,     &ReadOnlyHandler);
#else
    machine->SetHandler(PAGE_FAULT_EXCEPTION,    &DefaultHandler);
    machine->SetHandler(READ_ONLY_EXCEPTION,     &DefaultHandler);
#endif
    machine->SetHandler(BUS_ERROR_EXCEPTION,     &DefaultHandler);
    machine->SetHandler(ADDRESS_ERROR_EXCEPTION, &DefaultHandler);
    machine->SetHandler(OVERFLOW_EXCEPTION,      &DefaultHandler);
    machine->SetHandler(ILLEGAL_INSTR_EXCEPTION, &DefaultHandler);
}
