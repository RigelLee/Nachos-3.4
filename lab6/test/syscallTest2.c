#include "syscall.h"

void forktest() {
    Exit(0);
}

int main() {
    SpaceId sid = Exec("../test/matmult");
    Fork(forktest);
    Yield();
    Join(sid);
    Exit(0);
}