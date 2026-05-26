#ifndef BUS_H
#define BUS_H

#include "types.h"
#include "mapper.h"

void bus_reset();
void bus_set_mapper(Mapper *m);   /* NULL to disconnect */

Byte bus_read(Word addr);
void bus_write(Word addr, Byte data);

#endif
