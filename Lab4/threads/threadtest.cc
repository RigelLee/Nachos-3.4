// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "elevatortest.h"
#ifndef FILESYS
    #include "synch.h"
#endif

// testnum is set in main.cc
int testnum = 1;

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
	    printf("*** thread %d looped %d times\n", which, num);
        currentThread->Yield();
    }
}

void IdTest(int which)
{
    for (int i = 0; i < 2; i++){
        printf("*** thread %d (tid=%d,userId=%d) looped %d times\n", which, currentThread->getTid(), currentThread->getUsrID(), i);
        currentThread->Yield();
    }
}

void LoopThread(int which)
{
    for (int i = 0; i < 5; i++){
        printf("*** thread %d has run through a time slice\n", which);
        for (int j = 0; j < 10 * currentThread->getTimeSlice(); j++)
            interrupt->OneTick();
    }

    for (int i = 0; i < 5; i++){
        printf("*** thread %d hasn't run through a time slice\n", which);
        for (int j = 0; j < currentThread->getTimeSlice(); j++)
            interrupt->OneTick();
    }
    printf("*** thread %d finished\n", which);

}

void P_cond(ProducerConsumer_condition *p)
{
    p->Produce();
}

void C_cond(ProducerConsumer_condition *p)
{
    p->Consume();
}

void P_sema(ProducerConsumer_semaphore *p)
{
    p->Produce();
}

void C_sema(ProducerConsumer_semaphore *p)
{
    p->Consume();
}

void barrierTest(Barrier* barrier)
{
    for (int i = 0; i < 5; i++){
        printf("%s looped %d times.\n", currentThread->getName(), i);
        barrier->AlignedBarrier();
    }
}

void readTest(ReaderWriterLock* RW)
{
    for (int i = 0; i < 3; i++) {
        RW->readAcquire();
        printf("%s starts reading!\n", currentThread->getName());
        for (int j = 0; j < 10; j++)
            currentThread->Yield();
        printf("%s finishes reading!\n", currentThread->getName());
        RW->readRelease();
        for (int j = 0; j < 10; j++)
            currentThread->Yield();
    }
}

void writeTest(ReaderWriterLock* RW)
{
    for (int i = 0; i < 3; i++) {
        RW->writeAcquire();
        printf("%s starts writing!\n", currentThread->getName());
        for (int j = 0; j < 10; j++)
            currentThread->Yield();
        printf("%s finishes writeing!\n", currentThread->getName());
        RW->writeRelease();
        for (int j = 0; j < 10; j++)
            currentThread->Yield();
    }
}

//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread 
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, (void*)1);
    SimpleThread(0);
}

void ThreadTestInExercise3()
{
    DEBUG('t', "Enter ThreadTestInExercise3");

    int testNum = 5;
    int myUserId = 10;
    for (int i = 0; i < testNum; i++){
        Thread *t = new Thread("fork");

        t->setUserID(myUserId);
        t->Fork(IdTest, (void *)t->getTid());
    }

    currentThread->setUserID(myUserId);
    IdTest(currentThread->getTid());
}

void ThreadTestInExercise4_maxTid()
{
    DEBUG('t', "Enter ThreadTestInExercise4_maxTid");

    Thread *t;

    printf("Thread 0 (name=%s, tid=%d)\n", currentThread->getName(), currentThread->getTid());
    for (int i = 1; i <= 127; i++){
        t = new Thread("fork");
        printf("Thread %d (name=%s, tid=%d)\n", i, t->getName(), t->getTid());
    }
    
    printf("\nCreate the 128th thread:(counting from 0)\n");
    t = new Thread("test MAX_TID");
}

void ThreadTestInExercise4_TS()
{
    DEBUG('t', "Enter ThreadTestInExercise4_TS");

    Thread *t1 = new Thread("fork 1");
    Thread *t2 = new Thread("fork 2");

    t1->Fork(SimpleThread, (void*)1);

    TS();
}

void ThreadTestInLab2Exercise3()
{
    DEBUG('t', "Enter ThreadTestInLab2Exercise3");

    currentThread->setPri(0); currentThread->updateTimeSlice();

    Thread *t1 = new Thread("fork 1", 100);
    Thread *t2 = new Thread("fork 2", -1);

    t2->Fork(SimpleThread, (void*)2);
    TS();
    t1->Fork(SimpleThread, (void*)1);
    TS();

    SimpleThread(0);
}

void ThreadTestInLab2Challenge1()
{
    DEBUG('t', "Enter ThreadTestInLab2Challenge1");

    Thread *t1 = new Thread("fork 1");
    Thread *t2 = new Thread("fork 2");
    Thread *t3 = new Thread("fork 3");

    t1->Fork(LoopThread, (void*)1);
    t2->Fork(LoopThread, (void*)2);
    t3->Fork(LoopThread, (void*)3);

}

void ThreadTestInLab2Challenge2()
{
    DEBUG('t', "Enter ThreadTestInLab2Challenge2");

    scheduler->changePriority(currentThread, 20);

    Thread *t1 = new Thread("fork 1", 31);
    Thread *t2 = new Thread("fork 2", 16);
    Thread *t3 = new Thread("fork 3", 16);
    TS();

    t2->Fork(LoopThread, (void*)2);
    t3->Fork(LoopThread, (void*)3);
    t1->Fork(LoopThread, (void*)1);
    LoopThread(0);
    TS();
}

void ThreadTestInLab3Exercise4_condition()
{
    DEBUG('t', "ThreadTestInLab3Exercise4_condition");

    Thread *t1 = new Thread("Producer");
    Thread *t2 = new Thread("Consumer");
    TS();

    ProducerConsumer_condition *PC = new ProducerConsumer_condition(10);
    t1->Fork(P_cond, (void *)PC);
    t2->Fork(C_cond, (void *)PC);

    currentThread->setPri(0);
    currentThread->Yield();
    delete PC;
}

void ThreadTestInLab3Exercise4_semaphore()
{
    DEBUG('t', "ThreadTestInLab3Exercise4_condition");

    Thread *t1 = new Thread("Producer");
    Thread *t2 = new Thread("Consumer");
    TS();

    ProducerConsumer_semaphore *PC = new ProducerConsumer_semaphore(10);
    t1->Fork(P_sema, (void *)PC);
    t2->Fork(C_sema, (void *)PC);

    currentThread->setPri(0);
    currentThread->Yield();
    delete PC;
}

void ThreadTestInLab3Challenge1()
{
    DEBUG('t', "ThreadTestInLab3Challenge1");

    Thread *t1 = new Thread("fork 1");
    Thread *t2 = new Thread("fork 2");
    Thread *t3 = new Thread("fork 3");
    Thread *t4 = new Thread("fork 4");

    Barrier* barrier = new Barrier("BarrierTest", 4);

    t1->Fork(barrierTest, (void *)barrier);
    t2->Fork(barrierTest, (void *)barrier);
    t3->Fork(barrierTest, (void *)barrier);
    t4->Fork(barrierTest, (void *)barrier);

    currentThread->setPri(0);
    currentThread->Yield();
    delete barrier;
}

void ThreadTestInLab3Challenge2()
{
    DEBUG('t', "ThreadTestInLab3Challenge2");

    Thread *t1 = new Thread("reader 1");
    Thread *t2 = new Thread("wirter 1");
    Thread *t3 = new Thread("reader 2");
    Thread *t4 = new Thread("writer 2");

    ReaderWriterLock* RW = new ReaderWriterLock("BarrierTest");

    t1->Fork(readTest, (void *)RW);
    t2->Fork(writeTest, (void *)RW);
    t3->Fork(readTest, (void *)RW);
    t4->Fork(writeTest, (void *)RW);
    

    currentThread->setPri(0);
    currentThread->Yield();
    delete RW;
}

//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
    case 1:
	    ThreadTest1();
	break;
    case 2:
        ThreadTestInExercise3();
        break;
    case 3:
        ThreadTestInExercise4_maxTid();
        break;
    case 4:
        ThreadTestInExercise4_TS();
        break;
    case 5:
        ThreadTestInLab2Exercise3();
        break;
    case 6:
        ThreadTestInLab2Challenge1();
        break;
    case 7:
        ThreadTestInLab2Challenge2();
        break;
    case 8:
        ThreadTestInLab3Exercise4_condition();
        break;
    case 9:
        ThreadTestInLab3Exercise4_semaphore();
        break;
    case 10:
        ThreadTestInLab3Challenge1();
        break;
    case 11:
        ThreadTestInLab3Challenge2();
        break;
    default:
	    printf("No test specified.\n");
	break;
    }
}

