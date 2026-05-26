#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define MEM_SIZE (2 * 1024)   /* 0x0800 — NES internal RAM */

void mem_reset();

Byte mem_read(Word addr);
void mem_write(Word addr, Byte data);

#endif
