// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"
#include <time.h>
#include "openfile.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------



bool
FileHeader::Allocate(BitMap *freeMap, int fileSize, fileType FileType)
{ 
    numBytes = fileSize;
    type = FileType;
    numSectors  = divRoundUp(fileSize, SectorSize);

    int unassignedSectors = numSectors;
    if (unassignedSectors > NumDirect) {
        if (freeMap->NumClear() < NumDirect)
            return FALSE;
        for (int i = 0; i < NumDirect; i++)
            dataSectors[i] = freeMap->Find();
        
        dataSectors[NumDirect] = freeMap->Find();
        if (dataSectors[NumDirect] == -1)
            return FALSE;
        unassignedSectors -= NumDirect;

        int j = 0;
        int index[NumIndirect];
        while (unassignedSectors != 0){
            index[j] = freeMap->Find();
            if (index[j] == -1)
                return FALSE;

            int indexSectors = unassignedSectors > NumIndirect ? NumIndirect : unassignedSectors;
            int sectors[NumIndirect];
            if (freeMap->NumClear() < indexSectors)
                return FALSE;		
            for (int i = 0; i < indexSectors; i++)
                sectors[i] = freeMap->Find();
            synchDisk->WriteSector(index[j], (char *)sectors);
            unassignedSectors -= indexSectors;
            j++;
            if (j == NumIndirect)
                return FALSE;
        }
        synchDisk->WriteSector(dataSectors[NumDirect], (char *) index);
    }
    else {
        if (freeMap->NumClear() < unassignedSectors)
            return FALSE;		// not enough space
        for (int i = 0; i < unassignedSectors; i++)
            dataSectors[i] = freeMap->Find();
    }
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(BitMap *freeMap)
{
    if (numSectors > NumDirect) {
        int UnclearedSectors = numSectors;
        for (int i = 0; i < NumDirect; i++) {
            ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
            freeMap->Clear((int) dataSectors[i]);
        }
        UnclearedSectors -= NumDirect;

        int index[NumIndirect];
        synchDisk->ReadSector(dataSectors[NumDirect], (char *)index);
        int j = 0;
        while (UnclearedSectors != 0) {
            int indexSectors = UnclearedSectors > NumIndirect ? NumIndirect : UnclearedSectors;
            int sectors[NumIndirect];
            synchDisk->ReadSector(index[j], (char *)sectors);
            for (int i = 0; i < indexSectors; i++) {
                ASSERT(freeMap->Test((int) sectors[i]));  // ought to be marked!
                freeMap->Clear((int) sectors[i]);
            }
            ASSERT(freeMap->Test((int) index[j]));
            freeMap->Clear((int) index[j]);
            UnclearedSectors -= indexSectors;
            j++;
        }
    }
    else {
        for (int i = 0; i < numSectors; i++) {
            ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
            freeMap->Clear((int) dataSectors[i]);
        }
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    int flag = (offset == 896);
    int sizeDirectIndex = NumDirect * SectorSize;
    int sizePerIndex = NumIndirect * SectorSize;
    if (offset >= sizeDirectIndex) {
        offset -= sizeDirectIndex;
        int index[NumIndirect];
        synchDisk->ReadSector(dataSectors[NumDirect], (char *)index);
        int indexSector = index[offset / sizePerIndex];
        int sectors[NumIndirect];
        synchDisk->ReadSector(indexSector, (char *)sectors);
        return(sectors[(offset % sizePerIndex) / SectorSize]);
    }
    else
        return(dataSectors[offset / SectorSize]);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];
    char *typeName[5] = {"Normal File", "Directory File", "BitMap File", "Name File", "Path File"};
    int AllDataSectors[numSectors];

    printf("--------------- FileHeader contents Print ---------------\n");
    printf("File Type: %s\n", typeName[int(type)]);
    if (type == NormalFile || type == DirectoryFile) {
        printf("Create Time: %s\n", createTime);
        printf("Last Access Time: %s\n", lastAccessTime);
        printf("Last Modify Time: %s\n", lastModifyTime);
        char *path = new char[pathLength]; 
        getPath(path);
        printf("Path: %s\n", path);
    }
    printf("File size: %d\nFile blocks:\n", numBytes);
    k = 0;
    if (numSectors < NumDirect)
        for (i = 0; i < numSectors; i++) {
            AllDataSectors[k++] = dataSectors[i];
            printf("%d ", dataSectors[i]);
        }
    else {
        int UnprintedSectors = numSectors;
        for (i = 0; i < NumDirect; i++) {
            AllDataSectors[k++] = dataSectors[i];
            printf("%d ", dataSectors[i]);
        }
        UnprintedSectors -= NumDirect;
        int index[NumIndirect];
        synchDisk->ReadSector(dataSectors[NumDirect], (char *)index);
        j = 0;
        while (UnprintedSectors != 0) {
            int sectors[NumIndirect];
            synchDisk->ReadSector(index[j], (char *)sectors);
            int indexSectors = UnprintedSectors > NumIndirect ? NumIndirect : UnprintedSectors;
            for (i = 0; i < indexSectors; i++) {
                AllDataSectors[k++] = sectors[i];
                printf("%d ", sectors[i]);
            }
            UnprintedSectors -= indexSectors;
            j++;
        }
    }
    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
        synchDisk->ReadSector(AllDataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
            if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
                printf("%c", data[j]);
            else
                printf("\\%x", (unsigned char)data[j]);
        }
        printf("\n"); 
    }
    printf("\n"); 
    delete [] data;
}

void
FileHeader::setCreateTime()
{
    time_t _time;
    time(&_time);
    strncpy(createTime, asctime(gmtime(&_time)), TimeLength);
    createTime[TimeLength] = '\0';
    DEBUG('f', "Create file in %s\n", createTime);
}

void
FileHeader::setLastAccessTime()
{
    time_t _time;
    time(&_time);
    strncpy(lastAccessTime, asctime(gmtime(&_time)), TimeLength);
    lastAccessTime[TimeLength] = '\0';
    DEBUG('f', "Access file in %s\n", lastAccessTime);
}

void
FileHeader::setLastModifyTime()
{
    time_t _time;
    time(&_time);
    strncpy(lastModifyTime, asctime(gmtime(&_time)), TimeLength);
    lastModifyTime[TimeLength] = '\0';
    DEBUG('f', "Modify file in %s\n", lastModifyTime);
}

void
FileHeader::getPath(char *into)
{
    OpenFile *pathFile = new OpenFile(pathFileSector);
    pathFile->ReadAt(into, pathLength, 0);
    delete pathFile;
}

bool
FileHeader::Expand(BitMap *freeMap, int fileSize)
{
    ASSERT(fileSize > numBytes);

    numBytes = fileSize;
    int numNewSectors  = divRoundUp(fileSize, SectorSize);

    DEBUG('f', "Expand File!\n");
    if (numNewSectors == numSectors)
        return TRUE;

    int unassignedSectors = numNewSectors;
    if (unassignedSectors > NumDirect) {
        for (int i = 0; i < NumDirect; i++) {
            if (numNewSectors - unassignedSectors >= numSectors)
                dataSectors[i] = freeMap->Find();
            if (dataSectors[i] == -1)
                return FALSE;
            unassignedSectors--;
        }
        
        if (numNewSectors - unassignedSectors >= numSectors)
            dataSectors[NumDirect] = freeMap->Find();
        if (dataSectors[NumDirect] == -1)
            return FALSE;

        int j = 0;
        int index[NumIndirect];
        while (unassignedSectors != 0){
            if (numNewSectors - unassignedSectors >= numSectors)
                index[j] = freeMap->Find();
            if (index[j] == -1)
                return FALSE;

            int indexSectors = unassignedSectors > NumIndirect ? NumIndirect : unassignedSectors;
            int sectors[NumIndirect];	
            for (int i = 0; i < indexSectors; i++) {
                if (numNewSectors - unassignedSectors >= numSectors)
                    sectors[i] = freeMap->Find();
                if (sectors[i] == -1)
                    return FALSE;
                unassignedSectors--;
            }
            synchDisk->WriteSector(index[j], (char *)sectors);
            j++;
            if (j == NumIndirect)
                return FALSE;
        }
        synchDisk->WriteSector(dataSectors[NumDirect], (char *) index);
    }
    else {
        for (int i = 0; i < numNewSectors; i++) {
            if (numNewSectors - unassignedSectors >= numSectors)
                dataSectors[i] = freeMap->Find();
            if (dataSectors[i] == -1)
                return FALSE;
            unassignedSectors--;
        }
    }
    numSectors = numNewSectors;
    return TRUE;
}