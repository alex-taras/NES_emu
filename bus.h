#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include "types.h"
#include "mapper.h"
#include "ppu.h"
#include "controller.h"
#include "apu.h"

void bus_reset(void);
void bus_set_mapper(Mapper *m);   /* NULL to disconnect */
void bus_connect_ppu(PPU *ppu);   /* call once after ppu_init */
void bus_connect_controllers(Controller *c1, Controller *c2); /* c2 may be NULL */
void bus_connect_apu(APU *apu);

Byte bus_read(Word addr);
void bus_write(Word addr, Byte data);
int  bus_dma_active(void);
int  bus_dma_tick(uint64_t system_clock);  /* returns 1 if DMA still running */

#endif
