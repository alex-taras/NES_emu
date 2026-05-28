#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include "types.h"
#include "mapper.h"
#include "ppu.h"
#include "controller.h"
#include "apu.h"

typedef struct {
    uint64_t ppustatus_reads;
    uint64_t ppustatus_vblank_set_reads;
    uint64_t ppustatus_sprite0_set_reads;
    Byte     last_ppustatus_value;
    uint64_t ppuscroll_writes;
    uint64_t ppuaddr_writes;
    uint64_t ppudata_writes;
    uint64_t oamaddr_writes;
    Byte     last_oamaddr_value;
    uint64_t oamdata_writes;
    uint64_t oamdma_starts;
    Byte     last_oamdma_page;
} BusDebugStats;

void bus_reset(void);
void bus_set_mapper(Mapper *m);   /* NULL to disconnect */
void bus_connect_ppu(PPU *ppu);   /* call once after ppu_init */
void bus_connect_controllers(Controller *c1, Controller *c2); /* c2 may be NULL */
void bus_connect_apu(APU *apu);

Byte bus_read(Word addr);
void bus_write(Word addr, Byte data);
int  bus_dma_active(void);
int  bus_dma_tick(uint64_t system_clock);  /* returns 1 if DMA still running */
void bus_get_debug_stats(BusDebugStats *out_stats);
void bus_reset_debug_stats(void);
void bus_set_cpu_instruction_id(uint64_t instruction_id);

#endif
