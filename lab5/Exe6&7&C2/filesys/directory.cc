// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//	Fixing this is one of the parts to the assignment.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];
    tableSize = size;
    nameFileHdrSector = -1;
    for (int i = 0; i < tableSize; i++)
	    table[i].inUse = FALSE;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{ 
    delete [] table;
} 

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file)
{
    (void) file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
    (void) file->ReadAt((char *)&nameFileHdrSector, sizeof(int), tableSize * sizeof(DirectoryEntry));
    (void) file->ReadAt((char *)&nameFilePosition, sizeof(int), tableSize * sizeof(DirectoryEntry) + sizeof(int));
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file)
{
    (void) file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
    (void) file->WriteAt((char *)&nameFileHdrSector, sizeof(int), tableSize * sizeof(DirectoryEntry));
    (void) file->WriteAt((char *)&nameFilePosition, sizeof(int), tableSize * sizeof(DirectoryEntry) + sizeof(int));
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::FindIndex(char *name)
{
    char *nameBuffer;
    ASSERT(nameFileHdrSector != -1);
    OpenFile *nameFile = new OpenFile(nameFileHdrSector);
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse) {
            nameBuffer = new char[table[i].nameLength];
            nameFile->ReadAt(nameBuffer, table[i].nameLength, table[i].namePosition);
            if (!strncmp(nameBuffer, name, table[i].nameLength)){
                delete nameBuffer;
                delete nameFile;
	            return i;
            }
            delete nameBuffer;
        }
    delete nameFile;
    return -1;		// name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::Find(char *name)
{
    int i = FindIndex(name);

    if (i != -1)
	    return table[i].sector;
    return -1;
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::Add(char *name, int newSector)
{
    if (FindIndex(name) != -1)
	    return FALSE;
    ASSERT(nameFileHdrSector != -1);
    OpenFile *nameFile = new OpenFile(nameFileHdrSector);
    int len = strlen(name) + 1;

    for (int i = 0; i < tableSize; i++)
        if (!table[i].inUse) {
            table[i].inUse = TRUE;
            table[i].namePosition = nameFilePosition;
            table[i].nameLength = len;
            nameFile->WriteAt(name, len, nameFilePosition);
            nameFilePosition += len;
            table[i].sector = newSector;
            delete nameFile;
            return TRUE;
        }
    DirectoryEntry *oldTable = table;
    table = new DirectoryEntry[tableSize + 1];
    for (int i = 0; i < tableSize; i++)
        table[i] = oldTable[i];
    delete [] oldTable;
    tableSize++;
    table[tableSize - 1].inUse = TRUE;
    table[tableSize - 1].namePosition = nameFilePosition;
    table[tableSize - 1].nameLength = len;
    nameFile->WriteAt(name, len, nameFilePosition);
    nameFilePosition += len;
    table[tableSize - 1].sector = newSector;
    delete nameFile;
    return TRUE;	// no space.  Fix when we have extensible files.
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool
Directory::Remove(char *name)
{ 
    int i = FindIndex(name);

    if (i == -1)
	return FALSE; 		// name not in directory
    table[i].inUse = FALSE;
    return TRUE;	
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory. 
//----------------------------------------------------------------------

void
Directory::List()
{
    char *nameBuffer;
    ASSERT(nameFileHdrSector != -1);
    OpenFile *nameFile = new OpenFile(nameFileHdrSector);
    for (int i = 0; i < tableSize; i++)
	    if (table[i].inUse) {
            nameBuffer = new char[table[i].nameLength];
            nameFile->ReadAt(nameBuffer, table[i].nameLength, table[i].namePosition);
	        printf("%s\n", nameBuffer);
            delete nameBuffer;
        }
    delete nameFile;
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::Print()
{ 
    char *nameBuffer;
    ASSERT(nameFileHdrSector != -1);
    OpenFile *nameFile = new OpenFile(nameFileHdrSector);
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse) {
            nameBuffer = new char[table[i].nameLength];
            nameFile->ReadAt(nameBuffer, table[i].nameLength, table[i].namePosition);
            if (!strcmp(nameBuffer, ".") || !strcmp(nameBuffer, "..")) {
                delete nameBuffer;
                continue;
            }
            printf("Name: %s, Sector: %d\n", nameBuffer, table[i].sector);
            hdr->FetchFrom(table[i].sector);
            hdr->Print();
            if (hdr->getFileType() == DirectoryFile) {
                OpenFile *file = new OpenFile(table[i].sector);
                int dictSize = (hdr->FileLength() - 2 * sizeof(int)) / sizeof(DirectoryEntry);
                Directory *dict = new Directory(dictSize);
                dict->FetchFrom(file);
                dict->Print();
                delete dict;
                delete file;
            }
            delete nameBuffer;
        }
    printf("\n");
    delete hdr;
    delete nameFile;
}

bool
Directory::isEmpty()
{
    for (int i = 2; i < tableSize; i++) {
        if (table[i].inUse)
            return FALSE;
    }
    return TRUE;
}