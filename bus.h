#ifndef BUS_H
#define BUS_H

#include "types.h"

void bus_reset();

Byte bus_read(Word addr);
void bus_write(Word addr, Byte data);

#endif
