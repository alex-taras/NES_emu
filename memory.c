#include <string.h>

#include "memory.h"

static Byte mem[MEM_SIZE];

void mem_reset() {
    memset(mem, 0, sizeof(mem));
}

Byte mem_read(Word addr) {
    return mem[addr];
}

void mem_write(Word addr, Byte data) {
    mem[addr] = data;
}
