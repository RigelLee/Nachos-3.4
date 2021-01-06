#include "syscall.h"

#define fileLen 37
#define Content "This is the spring of our discontent."

int main() {
    int fd;
    int readn;
    char buffer[fileLen];

    Create("testFile");
    fd = Open("testFile");
    Write(Content, fileLen, fd);
    Close(fd);
    
    fd = Open("testFile");
    readn = Read(buffer, fileLen, fd);
    Close(fd);

    Halt();
}