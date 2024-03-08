/// Routines for synchronizing threads.
///
/// Three kinds of synchronization routines are defined here: semaphores,
/// locks and condition variables (the implementation of the last two are
/// left to the reader).
///
/// Any implementation of a synchronization routine needs some primitive
/// atomic operation.  We assume Nachos is running on a uniprocessor, and
/// thus atomicity can be provided by turning off interrupts.  While
/// interrupts are disabled, no context switch can occur, and thus the
/// current thread is guaranteed to hold the CPU throughout, until interrupts
/// are reenabled.
///
/// Because some of these routines might be called with interrupts already
/// disabled (`Semaphore::V` for one), instead of turning on interrupts at
/// the end of the atomic operation, we always simply re-set the interrupt
/// state back to its original value (whether that be disabled or enabled).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "synch.hh"
#include "system.hh"
#include <string.h>
#include <stdio.h>


/// Initialize a semaphore, so that it can be used for synchronization.
///
/// * `debugName` is an arbitrary name, useful for debugging.
/// * `initialValue` is the initial value of the semaphore.
Semaphore::Semaphore(const char *debugName, int initialValue)
{
    name  = debugName;
    value = initialValue;
    queue = new List<Thread *>;
}

/// De-allocate semaphore, when no longer needed.
///
/// Assume no one is still waiting on the semaphore!
Semaphore::~Semaphore()
{
    delete queue;
}

const char *
Semaphore::GetName() const
{
    return name;
}

/// Wait until semaphore `value > 0`, then decrement.
///
/// Checking the value and decrementing must be done atomically, so we need
/// to disable interrupts before checking the value.
///
/// Note that `Thread::Sleep` assumes that interrupts are disabled when it is
/// called.
void
Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(INT_OFF);
      // Disable interrupts.

    while (value == 0) {  // Semaphore not available.
        queue->Append(currentThread);  // So go to sleep.
        currentThread->Sleep();
    }
    value--;  // Semaphore available, consume its value.

    interrupt->SetLevel(oldLevel);  // Re-enable interrupts.
}

/// Increment semaphore value, waking up a waiter if necessary.
///
/// As with `P`, this operation must be atomic, so we need to disable
/// interrupts.  `Scheduler::ReadyToRun` assumes that threads are disabled
/// when it is called.
void
Semaphore::V()
{
    IntStatus oldLevel = interrupt->SetLevel(INT_OFF);

    Thread *thread = queue->Pop();
    if (thread != nullptr)
        // Make thread ready, consuming the `V` immediately.
        scheduler->ReadyToRun(thread);
    value++;

    interrupt->SetLevel(oldLevel);
}

/// Dummy functions -- so we can compile our later assignments.
///
/// Note -- without a correct implementation of `Condition::Wait`, the test
/// case in the network assignment will not work!

Lock::Lock(const char *debugName)
{
    unsigned nameLength = strlen(debugName) + 1;
    name = new char [nameLength];
    strncpy(name, debugName, nameLength);

    hasLock = nullptr;

    lockSemaphore = new Semaphore(name, 1);
}

Lock::~Lock()
{
    delete lockSemaphore;
    delete [] name;
}

const char *
Lock::GetName() const
{
    return name;
}

void
Lock::Acquire()
{
    ASSERT(!IsHeldByCurrentThread());

    if (hasLock != nullptr &&
        hasLock->GetPriority() < currentThread->GetPriority()) {
        unsigned newPriority = currentThread->GetPriority();
        hasLock->SetNewPriority(newPriority);
    }

    lockSemaphore->P();

    hasLock = currentThread;
}

void
Lock::Release()
{
    ASSERT(IsHeldByCurrentThread());

    currentThread->RestorePriority();

    hasLock = nullptr;

    lockSemaphore->V();
}

bool
Lock::IsHeldByCurrentThread() const
{
    return hasLock == currentThread;
}

Condition::Condition(const char *debugName, Lock *conditionLock)
{
    unsigned nameLength = strlen(debugName) + 1;
    name = new char [nameLength];
    strncpy(name, debugName, nameLength);

    lock = conditionLock;

    queue = new List<Semaphore *>;
}

Condition::~Condition()
{
    delete queue;
    delete [] name;
}

const char *
Condition::GetName() const
{
    return name;
}

void
Condition::Wait()
{
    ASSERT(lock->IsHeldByCurrentThread());

    Semaphore *sem = new Semaphore("Semaphore", 0);
    queue->Append(sem);

    lock->Release();

    sem->P();

    lock->Acquire();

    delete sem;
}

void
Condition::Signal()
{
    ASSERT(lock->IsHeldByCurrentThread());

    if (!queue->IsEmpty()) {
        Semaphore *sem = queue->Pop();
        sem->V();
    }
}

void
Condition::Broadcast()
{
    ASSERT(lock->IsHeldByCurrentThread());

    while (!queue->IsEmpty()) {
        Semaphore *sem = queue->Pop();
        sem->V();
    }
}

Channel::Channel(const char *debugName)
{
    unsigned nameLength = strlen(debugName) + 1;
    name = new char [nameLength];
    strncpy(name, debugName, nameLength);

    messageDest = nullptr;

    channelLock = new Lock(name);

    sender = new Condition("Sender", channelLock);
    receiver = new Condition("Receiver", channelLock);
    available = new Condition("Available", channelLock);
}

Channel::~Channel()
{
    delete sender;
    delete receiver;
    delete available;
    delete channelLock;
    delete [] name;
}

const char *
Channel::GetName() const
{
    return name;
}

void
Channel::Send(int message)
{
    channelLock->Acquire();

    while (messageDest == nullptr)
        receiver->Wait();

    *messageDest = message;
    sender->Signal();

    messageDest = nullptr;

    sender->Wait();

    available->Signal();

    channelLock->Release();
}

void
Channel::Receive(int *message)
{
    channelLock->Acquire();

    while (messageDest != nullptr)
        available->Wait();

    messageDest = message;

    receiver->Signal();

    sender->Wait();

    sender->Signal();

    channelLock->Release();
}
