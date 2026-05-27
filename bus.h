#ifndef BUS_H
#define BUS_H

#include "types.h"
#include "mapper.h"
#include "ppu.h"

void bus_reset();
void bus_set_mapper(Mapper *m);   /* NULL to disconnect */
void bus_connect_ppu(PPU *ppu);   /* call once after ppu_init */

Byte bus_read(Word addr);
void bus_write(Word addr, Byte data);
int  bus_consume_dma_stall(void); /* returns pending OAM DMA stall cycles, resets counter */

#endif
