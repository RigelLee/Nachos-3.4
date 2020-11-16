// synch.cc 
//	Routines for synchronizing threads.  Three kinds of
//	synchronization routines are defined here: semaphores, locks 
//   	and condition variables (the implementation of the last two
//	are left to the reader).
//
// Any implementation of a synchronization routine needs some
// primitive atomic operation.  We assume Nachos is running on
// a uniprocessor, and thus atomicity can be provided by
// turning off interrupts.  While interrupts are disabled, no
// context switch can occur, and thus the current thread is guaranteed
// to hold the CPU throughout, until interrupts are reenabled.
//
// Because some of these routines might be called with interrupts
// already disabled (Semaphore::V for one), instead of turning
// on interrupts at the end of the atomic operation, we always simply
// re-set the interrupt state back to its original value (whether
// that be disabled or enabled).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synch.h"
#include "system.h"

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	Initialize a semaphore, so that it can be used for synchronization.
//
//	"debugName" is an arbitrary name, useful for debugging.
//	"initialValue" is the initial value of the semaphore.
//----------------------------------------------------------------------

Semaphore::Semaphore(char* debugName, int initialValue)
{
    name = debugName;
    value = initialValue;
    queue = new List;
}

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	De-allocate semaphore, when no longer needed.  Assume no one
//	is still waiting on the semaphore!
//----------------------------------------------------------------------

Semaphore::~Semaphore()
{
    delete queue;
}

//----------------------------------------------------------------------
// Semaphore::P
// 	Wait until semaphore value > 0, then decrement.  Checking the
//	value and decrementing must be done atomically, so we
//	need to disable interrupts before checking the value.
//
//	Note that Thread::Sleep assumes that interrupts are disabled
//	when it is called.
//----------------------------------------------------------------------

void
Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);	// disable interrupts
    
    while (value == 0) { 			// semaphore not available
        queue->Append((void *)currentThread);	// so go to sleep
        currentThread->Sleep();
    } 
    value--; 					// semaphore available, 
						// consume its value
    
    (void) interrupt->SetLevel(oldLevel);	// re-enable interrupts
}

//----------------------------------------------------------------------
// Semaphore::V
// 	Increment semaphore value, waking up a waiter if necessary.
//	As with P(), this operation must be atomic, so we need to disable
//	interrupts.  Scheduler::ReadyToRun() assumes that threads
//	are disabled when it is called.
//----------------------------------------------------------------------

void
Semaphore::V()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    thread = (Thread *)queue->Remove();
    if (thread != NULL)	   // make thread ready, consuming the V immediately
	    scheduler->ReadyToRun(thread);
    value++;
    (void) interrupt->SetLevel(oldLevel);
}

// Dummy functions -- so we can compile our later assignments 
// Note -- without a correct implementation of Condition::Wait(), 
// the test case in the network assignment won't work!
Lock::Lock(char* debugName)
{
    name = debugName;
    lockSem = new Semaphore(debugName, 1);
    holdingThread = NULL;
}

Lock::~Lock()
{
    holdingThread = NULL;
    delete lockSem;
}

bool Lock::isHeldByCurrentThread()
{
    return currentThread == holdingThread;
}

void Lock::Acquire()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    lockSem->P();
    holdingThread = currentThread;

    (void) interrupt->SetLevel(oldLevel);
}

void Lock::Release()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    ASSERT(currentThread == holdingThread);

    holdingThread = NULL;
    lockSem->V();

    (void) interrupt->SetLevel(oldLevel);
}

Condition::Condition(char* debugName)
{
    name = debugName;
    count = 0;
    condSem = new Semaphore(debugName, 0);
}

Condition::~Condition()
{
    delete condSem;
}

void Condition::Wait(Lock* conditionLock)
{
    ASSERT(conditionLock->isHeldByCurrentThread());

    count++;
    conditionLock->Release();
    condSem->P();
    conditionLock->Acquire();
}

void Condition::Signal(Lock* conditionLock)
{
    ASSERT(conditionLock->isHeldByCurrentThread());
    if (count == 0)
        return;
    
    count--;
    condSem->V();
}

void Condition::Broadcast(Lock* conditionLock)
{
    ASSERT(conditionLock->isHeldByCurrentThread());

    while(count){
        count--;
        condSem->V();
    }
}


//My own

void itemPrint(int arg)
{
    int *item = (int *) arg;
    printf("%d, ", *item);
}

bounded_buffer::bounded_buffer(int buffer_size)
{
    size = buffer_size;
}

bounded_buffer::~bounded_buffer()
{
    //do nothing
}

bool bounded_buffer::IsFull()
{
    return NumInList() == size;
}

ProducerConsumer_condition::ProducerConsumer_condition(int buffer_size)
{
    buffer = new bounded_buffer(buffer_size);
    bufLock = new Lock("PCcond_lock");
    condPro = new Condition("pro_cond");
    condCon = new Condition("con_cond");
}

ProducerConsumer_condition::~ProducerConsumer_condition()
{
    delete condCon;
    delete condPro;
    delete bufLock;
    delete buffer;
}

static void
ProducerConsumer_condition::Produce()
{
    int count = 0;
    while (1) {
        int *item = new int;
        *item = count++;

        bufLock->Acquire();
        while (buffer->IsFull())
            condPro->Wait(bufLock);
        
        buffer->Append((void *) item);
        printf("\nProduces an integer variable with value of %d.\n", *item);
        printf("buffer: ");
        buffer->Mapcar(itemPrint);
        printf("\n");

        condCon->Signal(bufLock);
        bufLock->Release();
    }
}

static void
ProducerConsumer_condition::Consume()
{
    while (1) {
        bufLock->Acquire();
        while (buffer->IsEmpty())
            condCon->Wait(bufLock);
        
        int *item = (int *) buffer->Remove();
        printf("\nConsume an integer variable with value of %d.\n", *item);
        printf("buffer: ");
        buffer->Mapcar(itemPrint);
        printf("\n");
        delete item;

        condPro->Signal(bufLock);
        bufLock->Release();
    }
}

ProducerConsumer_semaphore::ProducerConsumer_semaphore(int buffer_size)
{
    buffer = new bounded_buffer(buffer_size);
    mutex = new Semaphore("PCcond_lock", 1);
    empty = new Semaphore("pro_lock", buffer_size);
    full = new Semaphore("con_lock", 0);
}

ProducerConsumer_semaphore::~ProducerConsumer_semaphore()
{
    delete full;
    delete empty;
    delete mutex;
    delete buffer;
}

void ProducerConsumer_semaphore::Produce()
{
    int count = 0;
    while (1) {
        int *item = new int;
        *item = count++;
        
        empty->P();
        mutex->P();
        
        buffer->Append((void *) item);
        printf("\nProduces an integer variable with value of %d.\n", *item);
        printf("buffer: ");
        buffer->Mapcar(itemPrint);
        printf("\n");

        mutex->V();
        full->V();
    }
}

void ProducerConsumer_semaphore::Consume()
{
    while (1) {
        full->P();
        mutex->P();
        
        int *item = (int *) buffer->Remove();
        printf("\nConsume an integer variable with value of %d.\n", *item);
        printf("buffer: ");
        buffer->Mapcar(itemPrint);
        printf("\n");
        delete item;

        mutex->V();
        empty->V();
    }
}


Barrier::Barrier(char* debugName, int threadNum)
{
    name = debugName;
    totThreadNum = threadNum;
    arrivedThreadNum = 0;
    conditionLock = new Lock("Barrier Lock");
    condIn = new Condition("Barrier In");
    condOut = new Condition("Barrier Out");
}

Barrier::~Barrier()
{
    delete condOut;
    delete condIn;
    delete conditionLock;
}

void Barrier::AlignedBarrier()
{
    conditionLock->Acquire();

    arrivedThreadNum++;
    if (arrivedThreadNum == totThreadNum)
        condIn->Broadcast(conditionLock);
    else
        condIn->Wait(conditionLock);

    arrivedThreadNum--;
    if (arrivedThreadNum == 0)
        condOut->Broadcast(conditionLock);
    else
        condOut->Wait(conditionLock);

    conditionLock->Release();
}

ReaderWriterLock::ReaderWriterLock(char* debugName)
{
    name = debugName;
    readersCount = 0;
    mutex = new Lock("mutex");
    writeLock = new Lock("writeLock");
}

ReaderWriterLock::~ReaderWriterLock()
{
    delete writeLock;
    delete mutex;
}

void ReaderWriterLock::readAcquire()
{
    mutex->Acquire();
    readersCount++;
    if (readersCount == 1)
        writeLock->Acquire();
    mutex->Release();
}

void ReaderWriterLock::readRelease()
{
    mutex->Acquire();
    readersCount--;
    if (writeLock->isHeldByCurrentThread()) {
        while (readersCount != 0) {
            mutex->Release();
            currentThread->Yield();
            mutex->Acquire();
        }
        writeLock->Release();
    }
    mutex->Release();
}

void ReaderWriterLock::writeAcquire()
{
    writeLock->Acquire();
}

void ReaderWriterLock::writeRelease()
{
    writeLock->Release();
}