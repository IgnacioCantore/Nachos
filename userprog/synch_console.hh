#ifndef NACHOS_USERPROG_SYNCHCONSOLE__HH
#define NACHOS_USERPROG_SYNCHCONSOLE__HH

#include "machine/console.hh"
#include "threads/synch.hh"


class SynchConsole {
public:

    SynchConsole();

    ~SynchConsole();

    char ReadChar();
    void WriteChar(char ch);

    unsigned ReadBuffer(char *buffer, unsigned size);
    void WriteBuffer(const char *buffer, unsigned size);

    void ReadAvail();
    void WriteDone();

private:
    Console *console;
    Semaphore *readAvail;
    Semaphore *writeDone;
    Lock *readLock;
    Lock *writeLock;
};

#endif
