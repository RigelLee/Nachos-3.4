// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "disk.h"
#include "bitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
#define NumDirEntries 		10
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries) + 2 * sizeof(int)

void setPathFile(char *path, char *name, FileHeader *directoryHdr, fileType type);

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG('f', "Initializing the file system.\n");
    if (format) {
        BitMap *freeMap = new BitMap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
        FileHeader *mapHdr = new FileHeader;
        FileHeader *dirHdr = new FileHeader;
        FileHeader *dirPathHdr = new FileHeader;

        DEBUG('f', "Formatting the file system.\n");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
        freeMap->Mark(FreeMapSector);	    
        freeMap->Mark(DirectorySector);

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

        ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize, BitMapFile));
        ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize, DirectoryFile));

        // Flush the bitmap and directory FileHeaders back to disk
        // We need to do this before we can "Open" the file, since open
        // reads the file header off of disk (and currently the disk has garbage
        // on it!).

        dirHdr->setCreateTime();
        dirHdr->setLastAccessTime();
        dirHdr->setLastModifyTime();
        int pathSector = freeMap->Find();
        ASSERT(pathSector != -1);
        ASSERT(dirPathHdr->Allocate(freeMap, SectorSize, PathFile));
        dirPathHdr->WriteBack(pathSector);
        delete dirPathHdr;
        OpenFile *pathFile = new OpenFile(pathSector);
        pathFile->WriteAt("/", 2, 0);
        delete pathFile;
        dirHdr->setPath(pathSector, 2);

        DEBUG('f', "Writing headers back to disk.\n");
        mapHdr->WriteBack(FreeMapSector);    
        dirHdr->WriteBack(DirectorySector);

        // OK to open the bitmap and directory files now
        // The file system operations assume these two files are left open
        // while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);

        int nameFileSector = freeMap->Find();
        ASSERT(nameFileSector != -1);
        FileHeader *nameFileHdr = new FileHeader;
        ASSERT(nameFileHdr->Allocate(freeMap, SectorSize, NameFile));
        nameFileHdr->WriteBack(nameFileSector);
        directory->initialNameFile(nameFileSector);
        delete nameFileHdr;

        directory->Add(".", DirectorySector);
        directory->Add("..", DirectorySector);
        
        // Once we have the files "open", we can write the initial version
        // of each file back to disk.  The directory at this point is completely
        // empty; but the bitmap has been changed to reflect the fact that
        // sectors on the disk have been allocated for the file headers and
        // to hold the file data for the directory and bitmap.

        DEBUG('f', "Writing bitmap and directory back to disk.\n");
        freeMap->WriteBack(freeMapFile);	 // flush changes to disk
        directory->WriteBack(directoryFile);

        if (DebugIsEnabled('f')) {
            freeMap->Print();
            directory->Print();
	    }
        delete freeMap; 
        delete directory; 
        delete mapHdr; 
        delete dirHdr;
    } else {
    // if we are not formatting the disk, just open the files representing
    // the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the dir ectory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool
FileSystem::Create(char *path, int initialSize)
{
    Directory *directory;
    BitMap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;
    int pathFileSector;
    FileHeader *pathFileHdr;
    DEBUG('f', "Creating file %s, size %d\n", path, initialSize);

    int dictHdrSector = findDict(path);
    if (dictHdrSector == -1) {
        printf("\nPath Error!\n");
        return FALSE;
    }
    FileHeader *dictHdr = new FileHeader;
    dictHdr->FetchFrom(dictHdrSector);
    OpenFile *dictFile = new OpenFile(dictHdrSector);

    int numDictEntries = (dictHdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
    directory = new Directory(numDictEntries);
    directory->FetchFrom(dictFile);

    char *name = path + strlen(path);
    while (*name != '/')
        name--;
    name++;

    fileType type;
    if (initialSize == -1) {    //create directory!
        type = DirectoryFile;
        initialSize = DirectoryFileSize;
    }
    else
        type = NormalFile;

    freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);

    int nameFileSector = -1;
    if (type == DirectoryFile) {
        nameFileSector = freeMap->Find();
        if (nameFileSector == -1)
            return FALSE;
        FileHeader *nameFileHdr = new FileHeader;
        if (nameFileHdr->Allocate(freeMap, SectorSize, NameFile) == FALSE)
            return FALSE;
        nameFileHdr->WriteBack(nameFileSector);
        delete nameFileHdr;
    }

    if (directory->Find(name) != -1)
        success = FALSE;			// file is already in directory
    else {
        sector = freeMap->Find();	// find a sector to hold the file header
        pathFileSector = freeMap->Find();
    	if (sector == -1) 		
            success = FALSE;		// no free block for file header 
        else if (pathFileSector == -1)
            success = FALSE;
        else if (!directory->Add(name, sector))
            success = FALSE;	// no space in directory
	    else {
    	    hdr = new FileHeader;
            pathFileHdr = new FileHeader;
            if (!hdr->Allocate(freeMap, initialSize, type))
                success = FALSE;	// no space on disk for data
            else if (!pathFileHdr->Allocate(freeMap, SectorSize, PathFile))
                success = FALSE;
            else {	
                success = TRUE;
                // everthing worked, flush all changes back to disk
                hdr->setCreateTime();
                hdr->setLastAccessTime();
                hdr->setLastModifyTime();
                hdr->setPath(pathFileSector, dictHdr->getPathLength() + strlen(name) + (type == DirectoryFile));

                hdr->WriteBack(sector);
                pathFileHdr->WriteBack(pathFileSector); 		
                directory->WriteBack(dictFile);
                freeMap->WriteBack(freeMapFile);

                if (type == DirectoryFile) {
                    Directory *newDict = new Directory(NumDirEntries);
                    newDict->initialNameFile(nameFileSector);
                    newDict->Add(".", sector);
                    newDict->Add("..", dictHdrSector); 
                    OpenFile *file = new OpenFile(sector);
                    newDict->WriteBack(file);
                    delete file;
                    delete newDict;
                }

                OpenFile *pathFile = new OpenFile(pathFileSector);
                char *filePath;
                filePath = new char[dictHdr->getPathLength() + strlen(name) + (type == DirectoryFile)];
                setPathFile(filePath, name, dictHdr, type);
                pathFile->WriteAt(filePath, dictHdr->getPathLength() + strlen(name) + (type == DirectoryFile), 0);
                delete pathFile;
                delete filePath;
            }
            delete pathFileHdr;
            delete hdr;
	    }
    }
    delete freeMap;
    delete directory;
    delete dictFile;
    delete dictHdr;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *path)
{ 
    int dictHdrSector = findDict(path);
    if (dictHdrSector == -1) {
        printf("\nPath Error!\n");
        return NULL;
    }
    FileHeader *dictHdr = new FileHeader;
    dictHdr->FetchFrom(dictHdrSector);
    OpenFile *dictFile = new OpenFile(dictHdrSector);

    int numDictEntries = (dictHdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
    Directory *directory = new Directory(numDictEntries);
    OpenFile *openFile = NULL;

    char *name = path + strlen(path);
    while (*name != '/')
        name--;
    name++;

    int sector;
    DEBUG('f', "Opening file %s\n", path);
    directory->FetchFrom(dictFile);
    sector = directory->Find(name); 
    if (sector >= 0) 		
	    openFile = new OpenFile(sector);	// name was found in directory 
    delete directory;
    delete dictFile;
    delete dictHdr;
    return openFile;				// return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *path)
{ 
    Directory *directory;
    BitMap *freeMap;
    FileHeader *fileHdr;
    int sector;

    int dictHdrSector = findDict(path);
    if (dictHdrSector == -1) {
        printf("\nPath Error!\n");
        return FALSE;
    }
    FileHeader *dictHdr = new FileHeader;
    dictHdr->FetchFrom(dictHdrSector);
    OpenFile *dictFile = new OpenFile(dictHdrSector);

    int numDictEntries = (dictHdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
    directory = new Directory(numDictEntries);
    directory->FetchFrom(dictFile);

    char *name = path + strlen(path);
    while (*name != '/')
        name--;
    name++;

    sector = directory->Find(name);
    if (sector == -1) {
       delete directory;
       delete dictFile;
       delete dictHdr;
       return FALSE;			 // file not found 
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);

    if (fileHdr->getFileType() == DirectoryFile) {
        OpenFile *file = new OpenFile(sector);
        int numEntries = (fileHdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
        Directory *dir = new Directory(numEntries);
        dir->FetchFrom(file);
        if (!dir->isEmpty()) {
            printf("Can't remove directory which is not empty!\n");
            delete dir;
            delete file;
            delete directory;
            delete dictFile;
            delete dictHdr;
            return FALSE;
        }

        FileHeader *nameFileHdr = new FileHeader;
        nameFileHdr->FetchFrom(dir->getNameFileSector());
        nameFileHdr->Deallocate(freeMap);
        delete nameFileHdr;
        freeMap->Clear(dir->getNameFileSector());
        delete dir;
        delete file;
    }

    // Delete Path File
    int pathSector = fileHdr->getPathFileSector();
    FileHeader *pathFileHdr = new FileHeader;
    pathFileHdr->FetchFrom(pathSector);
    pathFileHdr->Deallocate(freeMap);
    delete pathFileHdr;
    freeMap->Clear(pathSector);

    fileHdr->Deallocate(freeMap);  		// remove data blocks
    freeMap->Clear(sector);			// remove header block
    directory->Remove(name);

    freeMap->WriteBack(freeMapFile);		// flush to disk
    directory->WriteBack(dictFile);        // flush to disk
    delete fileHdr;
    delete directory;
    delete dictFile;
    delete dictHdr;
    delete freeMap;
    return TRUE;
} 

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List()
{
    Directory *directory = new Directory(NumDirEntries);

    directory->FetchFrom(directoryFile);
    directory->List();
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    BitMap *freeMap = new BitMap(NumSectors);
    int numDictEntries = (dirHdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
    Directory *directory = new Directory(numDictEntries);

    freeMap->FetchFrom(freeMapFile);
    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
} 

void setPathFile(char *path, char *name, FileHeader *directoryHdr, fileType type)
{
    directoryHdr->getPath(path);
    strcat(path, name);
    if (type == DirectoryFile) {
        path[directoryHdr->getPathLength() + strlen(name) - 1] = '/';
        path[directoryHdr->getPathLength() + strlen(name)] = '\0';
    }
}

int 
FileSystem::findDict(char *path)
{
    if (*path != '/')
        return -1;
    path += 1;

    int dictSector = DirectorySector;
    while (1) {
        int len;
        for (len = 0; len < strlen(path); len++)
            if (*(path + len) == '/')
                break;
        if (len == strlen(path))
            return dictSector;
        char nxtDictName[len + 1];
        for (int i = 0; i < len; i++)
            nxtDictName[i] = path[i];
        nxtDictName[len] = '\0';

        OpenFile *dictFile = new OpenFile(dictSector);
        FileHeader *dictHdr = new FileHeader;
        dictHdr->FetchFrom(dictSector);
        int numDictEntries = (dictHdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
        Directory *dict = new Directory(numDictEntries);
        dict->FetchFrom(dictFile);

        dictSector = dict->Find(nxtDictName);
        if (dictSector == -1)
            return -1;
        path += len + 1;
        delete dict;
        delete dictHdr;
        delete dictFile;
    }
}

bool
FileSystem::ExpandFile(FileHeader *hdr, int newSize)
{
    BitMap *freeMap = new BitMap(FreeMapFileSize);
    freeMap->FetchFrom(freeMapFile);
    hdr->Expand(freeMap, newSize);
    freeMap->WriteBack(freeMapFile);
    delete freeMap;
}