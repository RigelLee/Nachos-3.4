// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------

void Simple_TLBReplaceHandler(unsigned int vpn) {
    int index = vpn % TLBSize;
    if (machine->tlb[index].valid && machine->tlb[index].dirty) {
        unsigned int Replaced_vpn = machine->tlb[index].virtualPage;
        machine->pageTable[Replaced_vpn].dirty = true;
    }

    machine->tlb[index] = machine->pageTable[vpn];
}

void FIFO_TLBReplaceHandler(unsigned int vpn) {
    int index = 0;
    for (int i = 0; i < TLBSize; i++)
        if (machine->tlb[i].valid == false){
            index = i;
            break;
        }
    if (machine->tlb[index].valid && machine->tlb[index].dirty) {
        unsigned int Replaced_vpn = machine->tlb[index].virtualPage;
        machine->pageTable[Replaced_vpn].dirty = true;
    }

    for (int i = index; i < TLBSize - 1; i++)
        machine->tlb[i] = machine->tlb[i+1];
    machine->tlb[TLBSize-1] = machine->pageTable[vpn];

}

void LRU_TLBReplaceHandler(unsigned int vpn) {
#ifdef USE_IPT
    int ppn = vpn;
#endif
    for (int i = 0; i < TLBSize; i++)
        if (!machine->tlb[i].valid){
#ifdef USE_IPT
            machine->tlb[i] = machine->InvertedPageTable[ppn];
#else
            machine->tlb[i] = machine->pageTable[vpn];
#endif
            return;
        }

    int index = 0;
    unsigned int minRecord = (unsigned int) -1;
    for (int i = 0; i < TLBSize; i++)
        if (machine->tlb[i].LRUrecord < minRecord){
            minRecord = machine->tlb[i].LRUrecord;
            index = i;
        }
#ifdef USE_IPT
    if (machine->tlb[index].dirty) {
        unsigned int Replaced_ppn = machine->tlb[index].physicalPage;
        machine->InvertedPageTable[Replaced_ppn].dirty = true;
    }
    machine->tlb[index] = machine->InvertedPageTable[ppn];
#else
    if (machine->tlb[index].dirty) {
        unsigned int Replaced_vpn = machine->tlb[index].virtualPage;
        machine->pageTable[Replaced_vpn].dirty = true;
    }
    machine->tlb[index] = machine->pageTable[vpn];
#endif
}

int LRU_LocalPageFrameReplaceHandler() {
    int index = -1;
    unsigned int minRecord = (unsigned int) -1;
    for (int i = 0; i < machine->pageTableSize; i++)
        if (machine->pageTable[i].valid && machine->pageTable[i].LRUrecord < minRecord){
            minRecord = machine->tlb[i].LRUrecord;
            index = i;
        }
    if (index == -1)
        return -1;

    bool IsDirty = machine->pageTable[index].dirty;
    machine->pageTable[index].valid = false;
    for (int i = 0; i < TLBSize; i++)
        if (machine->tlb[i].valid && machine->tlb[i].virtualPage == index) {
            IsDirty = machine->tlb[i].dirty;
            machine->tlb[i].valid = false;
            break;
        }

    if (IsDirty) {
        char fileName[13] = "vm_";
        char tid[10];
        sprintf(tid, "%d", currentThread->getTid());
        strcat(fileName, tid);
        OpenFile *vmOnDisk = fileSystem->Open(fileName);
        vmOnDisk->WriteAt(&(machine->mainMemory[machine->pageTable[index].physicalPage * PageSize])
                            , PageSize, index * PageSize);
        delete vmOnDisk;
    }

    return machine->pageTable[index].physicalPage;
}

int LRU_GlobalPageFrameReplaceHandler() {
    int index = -1;
    unsigned int minRecord = (unsigned int) -1;
    for (int i = 0; i < NumPhysPages; i++)
        if (machine->InvertedPageTable[i].LRUrecord < minRecord) {
            minRecord = machine->InvertedPageTable[i].LRUrecord;
            index = i;
        }

    bool IsDirty = machine->InvertedPageTable[index].dirty;
    machine->InvertedPageTable[index].valid = false;
    for (int i = 0; i < TLBSize; i++)
        if (machine->tlb[i].valid && machine->tlb[i].physicalPage == index) {
            IsDirty = machine->tlb[i].dirty;
            machine->tlb[i].valid = false;
            break;
        }

    if (IsDirty) {
        char fileName[13] = "vm_";
        char tid[10];
#ifdef USE_IPT
        sprintf(tid, "%d", machine->InvertedPageTable[index].tid);
#else
        sprintf(tid, "%d", currentThread->getTid());
#endif
        strcat(fileName, tid);
        OpenFile *vmOnDisk = fileSystem->Open(fileName);
        vmOnDisk->WriteAt(&(machine->mainMemory[index * PageSize])
                            , PageSize, machine->InvertedPageTable[index].virtualPage * PageSize);
        delete vmOnDisk;
    }

    return index;
}

int PageFaultHandler(unsigned int vpn) {
    int PF2Place = machine->allocatePageFrame();
    if (PF2Place == -1) {
#ifdef USE_IPT
        PF2Place = LRU_GlobalPageFrameReplaceHandler();
#else
        PF2Place = LRU_LocalPageFrameReplaceHandler();
        if (PF2Place == -1)
            return -1;
#endif
    }
    
    DEBUG('a', "Page Fault: Loading page from disk!");
    char fileName[13] = "vm_";
    char tid[10];
    sprintf(tid, "%d", currentThread->getTid());
    strcat(fileName, tid);
    OpenFile *vmOnDisk = fileSystem->Open(fileName);
    ASSERT(vmOnDisk != NULL);

    vmOnDisk->ReadAt(&(machine->mainMemory[PF2Place * PageSize]), PageSize, vpn * PageSize);
    delete vmOnDisk;

    TranslationEntry *PTE;
#ifdef USE_IPT
    PTE = &(machine->InvertedPageTable[PF2Place]);
#else
    PTE = &(machine->pageTable[vpn]);
#endif

    PTE->virtualPage = vpn;
    PTE->physicalPage = PF2Place;
    PTE->valid = true;
    PTE->use = false;
    PTE->readOnly = false;
    PTE->dirty = false;
#ifdef USE_IPT
    PTE->tid = currentThread->getTid();
#endif

    return PF2Place;
}

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if ((which == SyscallException) && (type == SC_Halt)) {
        DEBUG('a', "Shutdown, initiated by user program.\n");
        interrupt->Halt();
    } 
    else if ((which == SyscallException) && (type == SC_Exit)) {
        DEBUG('a', "Exiting user program.\n");
        int exitValue = machine->ReadRegister(4);
        if (exitValue == 0)
            DEBUG('s', "With value 0, User program (tid=%d) exits normally.\n\n", currentThread->getTid());
        else
            DEBUG('s', "User program (tid=%d) exit with value %d!\n\n", currentThread->getTid(), exitValue);

        if (currentThread->space != NULL) {
            delete currentThread->space;
            currentThread->space = NULL;
        }

        char fileName[13] = "vm_";
        char tid[10];
        sprintf(tid, "%d", currentThread->getTid());
        strcat(fileName, tid);
        fileSystem->Remove(fileName);

        currentThread->Finish();
    } 
    else if(which == PageFaultException){
        unsigned int vAddr = machine->ReadRegister(BadVAddrReg);
        unsigned int vpn = vAddr / PageSize;

#ifdef USE_IPT
        int ppn = -1;
        for (int i = 0; i < NumPhysPages; i++)
            if (machine->InvertedPageTable[i].valid && machine->InvertedPageTable[i].tid == currentThread->getTid()
                             && machine->InvertedPageTable[i].virtualPage == vpn)
                ppn = i;
        if (ppn == -1)
            ppn = PageFaultHandler(vpn);
        if (machine->tlb != NULL)
            LRU_TLBReplaceHandler(ppn);
#else
        if (!machine->pageTable[vpn].valid)
            if(PageFaultHandler(vpn) == -1) {
                currentThread->Yield();
                return;
            }

        if (machine->tlb != NULL)
            LRU_TLBReplaceHandler(vpn);
#endif
    }
    else {
        printf("Unexpected user mode exception %d %d\n", which, type);
        ASSERT(FALSE);
    }
}
