/// Simple test case for the threads assignment.
///
/// Create several threads, and have them context switch back and forth
/// between themselves by calling `Thread::Yield`, to illustrate the inner
/// workings of the thread system.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2007-2009 Universidad de Las Palmas de Gran Canaria.
///               2016-2020 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include <stdio.h>
#include <string.h>
#include "system.hh"
#include "synch.hh"
#include <unistd.h>
#include <stdlib.h>

#ifdef SEMAPHORE_TEST

Semaphore *s = new Semaphore("Semaforo", 3);

#endif

/// Loop 10 times, yielding the CPU to another ready thread each iteration.
///
/// * `name` points to a string with a thread name, just for debugging
///   purposes.
void
SimpleThread(void *name_)
{
    // Reinterpret arg `name` as a string.
    char *name = (char *) name_;

    #ifdef SEMAPHORE_TEST

    s->P();
    DEBUG('s', "%s thread called P()\n", name);

    #endif
    // If the lines dealing with interrupts are commented, the code will
    // behave incorrectly, because printf execution may cause race
    // conditions.
    for (unsigned num = 0; num < 10; num++) {
        printf("*** Thread `%s` is running: iteration %u\n", name, num);
        currentThread->Yield();
    }

    #ifdef SEMAPHORE_TEST

    s->V();
    DEBUG('s', "%s thread called V()\n", name);

    #endif

    printf("!!! Thread `%s` has finished\n", name);
}


#define N 10
#define DELAY sleep(random() % 5)

int buffer[N];
int in = 0;
int out = 0;
int amount = 0;

Lock *lock = new Lock("Lock");

Condition *full = new Condition("Full", lock);
Condition *empty = new Condition("Empty", lock);

void
Producer(void *arg)
{
    int item = 1;
    
    for (;;) {
        lock->Acquire();
        
        while (amount == N)
            full->Wait();
            
        buffer[in] = item;
        
        printf("Producing: buffer[%d] = %d\n", in, item);
        
        in = (in + 1) % N;
        item++;
        amount++;
        
        empty->Signal();
        lock->Release();
        
        DELAY;
    }
}

void
Consumer(void *arg)
{
    for (;;) {
        lock->Acquire();
        
        while (amount == 0)
            empty->Wait();
            
        printf("Consuming: buffer[%d] = %d\n", out, buffer[out]);
        
        out = (out + 1) % N;
        
        amount--;
        
        full->Signal();
        lock->Release();
        
        DELAY;
    }
}


Channel *testChannel = new Channel("TestChannel");

void
senderTest(void *arg)
{
    int message = *(int *) arg;
    testChannel->Send(message);
    
    printf("Sent message is %d\n", message);
}

void
receiverTest(void *arg)
{
    int message;
    testChannel->Receive(&message);
    
    printf("Received message is %d\n", message);
}



void
ThreadTest()
{
    DEBUG('t', "Entering channel test\n");

    int n = 25;
    
    Thread *senderThread = new Thread("Sender", false, 2);
    senderThread->Fork(senderTest, (void *) &n);
    
    receiverTest(nullptr);
}
