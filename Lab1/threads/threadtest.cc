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

    printf("--------------Invoke TS()--------------\n");
    TS();
    printf("--------------Exit TS()--------------\n");
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
    default:
	printf("No test specified.\n");
	break;
    }
}

