#include "synch_console.hh"

static void
ConsoleReadAvail(void *arg)
{
    ASSERT(arg != nullptr);
    SynchConsole *console = (SynchConsole *) arg;
    console->ReadAvail();
}

static void
ConsoleWriteDone(void *arg)
{
    ASSERT(arg != nullptr);
    SynchConsole *console = (SynchConsole *) arg;
    console->WriteDone();
}

SynchConsole::SynchConsole()
{
    console   = new Console(nullptr, nullptr, ConsoleReadAvail,
                            ConsoleWriteDone, this);
    readAvail = new Semaphore("read avail", 0);
    writeDone = new Semaphore("write done", 0);
    readLock  = new Lock("read console lock");
    writeLock = new Lock("write console lock");
}

SynchConsole::~SynchConsole()
{
    delete console;
    delete readAvail;
    delete writeDone;
    delete readLock;
    delete writeLock;
}

char
SynchConsole::ReadChar()
{
    readAvail->P();
    return console->GetChar();
}

void
SynchConsole::WriteChar(char ch)
{
    console->PutChar(ch);
    writeDone->P();
}

unsigned
SynchConsole::ReadBuffer(char *buffer, unsigned size)
{
    ASSERT(buffer != nullptr);

    unsigned i = 0;
    readLock->Acquire();
    for (; i < size; i++) {
        char ch = ReadChar();
        buffer[i] = ch;
        if (ch == '\n')
            break;
    }
    buffer[i + 1] = '\0';
    readLock->Release();
    return i;
}

void
SynchConsole::WriteBuffer(const char *buffer, unsigned size)
{
    ASSERT(buffer != nullptr);

    writeLock->Acquire();
    for (unsigned i = 0; i < size; i++)
        WriteChar(buffer[i]);
    writeLock->Release();
}

void
SynchConsole::ReadAvail()
{
    readAvail->V();
}

void
SynchConsole::WriteDone()
{
    writeDone->V();
}
