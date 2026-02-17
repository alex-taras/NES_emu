#include "bus.h"
#include "memory.h"

void bus_reset() {
    mem_reset();
}

#define MEM_START 0x0000
#define MEM_END   0xFFFF

Byte bus_read(Word addr) {
    if (addr >= MEM_START && addr <= MEM_END) {
        return mem_read(addr);
    }

    return 0x00; // default value for unmapped addresses
}

void bus_write(Word addr, Byte data) {
    if (addr >= MEM_START && addr <= MEM_END) {
        mem_write(addr, data);
    }

    // ignore writes to unmapped addresses
}
