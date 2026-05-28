#include "bus.h"
#include "memory.h"
#include "controller.h"
#include <stdio.h>

static Mapper *active_mapper = NULL;
static PPU *active_ppu = NULL;
static Controller *ctrl1 = NULL;
static Controller *ctrl2 = NULL;
static APU *active_apu = NULL;

/* DMA state */
static int   dma_transfer = 0;   /* 1 = DMA in progress */
static int   dma_dummy    = 1;   /* 1 = waiting for alignment cycle */
static Byte  dma_page     = 0;   /* high byte of source address */
static Byte  dma_addr     = 0;   /* current byte index 0–255 */
static Byte  dma_data     = 0;   /* read buffer */

/* Fallback for when no mapper is connected.
   Covers only 0xFFFE–0xFFFF so the BRK test (which writes the IRQ vector
   directly via bus_write) still works without a cartridge. */
static Byte irq_vector_fallback[2];
static BusDebugStats debug_stats;
static uint64_t current_instruction_id = 0;
static uint64_t last_mmc1_write_instruction_id = UINT64_MAX;

void bus_reset_debug_stats(void) {
    debug_stats.ppustatus_reads = 0;
    debug_stats.ppustatus_vblank_set_reads = 0;
    debug_stats.ppustatus_sprite0_set_reads = 0;
    debug_stats.last_ppustatus_value = 0;
    debug_stats.ppuscroll_writes = 0;
    debug_stats.ppuaddr_writes = 0;
    debug_stats.ppudata_writes = 0;
    debug_stats.oamaddr_writes = 0;
    debug_stats.last_oamaddr_value = 0;
    debug_stats.oamdata_writes = 0;
    debug_stats.oamdma_starts = 0;
    debug_stats.last_oamdma_page = 0;
}

void bus_get_debug_stats(BusDebugStats *out_stats) {
    if (out_stats) *out_stats = debug_stats;
}

void bus_set_cpu_instruction_id(uint64_t instruction_id) {
    current_instruction_id = instruction_id;
}

void bus_reset() {
    mem_reset();
    irq_vector_fallback[0] = 0x00;
    irq_vector_fallback[1] = 0x00;
    /* active_mapper is NOT cleared here — soft reset must keep mapper attached */
    /* active_ppu is NOT cleared here either - same reasoning */
    dma_transfer = 0;
    dma_dummy    = 1;
    dma_page     = 0;
    dma_addr     = 0;
    dma_data     = 0;
    current_instruction_id = 0;
    last_mmc1_write_instruction_id = UINT64_MAX;
    bus_reset_debug_stats();
}

void bus_set_mapper(Mapper *m) {
    active_mapper = m;
}

void bus_connect_ppu(PPU *ppu) {
    active_ppu = ppu;
}

void bus_connect_controllers(Controller *c1, Controller *c2) {
    ctrl1 = c1;
    ctrl2 = c2;
}

void bus_connect_apu(APU *apu) {
    active_apu = apu;
}

Byte bus_read(Word addr) {
    if (addr <= 0x1FFF) {
        return mem_read(addr & 0x07FF);
    }
    if (addr <= 0x3FFF) {
        if (active_ppu) {
            Byte reg = addr & 0x07;
            Byte val = ppu_reg_read(active_ppu, reg);
            if (reg == 0x02) {
                debug_stats.ppustatus_reads++;
                debug_stats.last_ppustatus_value = val;
                if (val & 0x80) debug_stats.ppustatus_vblank_set_reads++;
                if (val & 0x40) debug_stats.ppustatus_sprite0_set_reads++;
                if (debug_stats.ppustatus_reads == 10000 ||
                    debug_stats.ppustatus_reads == 50000 ||
                    debug_stats.ppustatus_reads == 100000) {
                    fprintf(stderr,
                            "PPUSTATUS_POLL: reads=%llu val=%02X vblank_reads=%llu sp0_reads=%llu | PPU sl=%d dot=%d status=%02X ctrl=%02X mask=%02X v=%04X t=%04X x=%d w=%d | OAM0 Y=%u tile=%02X attr=%02X X=%u\n",
                            (unsigned long long)debug_stats.ppustatus_reads,
                            val,
                            (unsigned long long)debug_stats.ppustatus_vblank_set_reads,
                            (unsigned long long)debug_stats.ppustatus_sprite0_set_reads,
                            active_ppu->scanline, active_ppu->dot, active_ppu->status,
                            active_ppu->ctrl, active_ppu->mask, active_ppu->v, active_ppu->t,
                            active_ppu->x, active_ppu->w,
                            active_ppu->oam[0], active_ppu->oam[1], active_ppu->oam[2], active_ppu->oam[3]);
                }
            }
            return val;
        }
        return 0x00;
    }
    if (addr <= 0x401F) {
        if (addr == 0x4016) return ctrl1 ? controller_read(ctrl1) : 0x00;
        if (addr == 0x4017) return ctrl2 ? controller_read(ctrl2) : 0x00;
        if (addr == 0x4015) return active_apu ? apu_read(active_apu, addr) : 0x00;
        return 0x00;
    }
    /* 0x4020–0xFFFF: cartridge space */
    if (active_mapper) {
        return mapper_prg_read(active_mapper, addr);
    }
    /* No mapper: IRQ vector fallback for test compatibility */
    if (addr == 0xFFFE) return irq_vector_fallback[0];
    if (addr == 0xFFFF) return irq_vector_fallback[1];
    return 0x00;
}

void bus_write(Word addr, Byte data) {
    if (addr <= 0x1FFF) {
        mem_write(addr & 0x07FF, data);
        return;
    }
    if (addr <= 0x3FFF) {
        if (active_ppu) {
            Byte reg = addr & 0x07;
            if (reg == 0x03) {
                debug_stats.oamaddr_writes++;
                debug_stats.last_oamaddr_value = data;
            } else if (reg == 0x04) {
                debug_stats.oamdata_writes++;
            } else if (reg == 0x05) {
                debug_stats.ppuscroll_writes++;
            } else if (reg == 0x06) {
                debug_stats.ppuaddr_writes++;
            } else if (reg == 0x07) {
                debug_stats.ppudata_writes++;
            }
            ppu_reg_write(active_ppu, reg, data);
        }
        return;
    }
    if (addr == 0x4014) {
        /* OAM DMA: copy 256 bytes from CPU page $XX00-$XXFF to PPU OAM */
        debug_stats.oamdma_starts++;
        debug_stats.last_oamdma_page = data;
        dma_page     = data;
        dma_addr     = 0x00;
        dma_transfer = 1;
        dma_dummy    = 1;
        return;
    }
    if (addr == 0x4016) {
        if (ctrl1) controller_write(ctrl1, data);
        if (ctrl2) controller_write(ctrl2, data); /* strobe is broadcast to both */
        return;
    }
    if (addr >= 0x4000 && addr <= 0x4013) {
        if (active_apu) apu_write(active_apu, addr, data);
        return;
    }
    if (addr == 0x4015) {
        if (active_apu) apu_write(active_apu, addr, data);
        return;
    }
    if (addr == 0x4017) {
        if (active_apu) apu_write(active_apu, addr, data);
        return;
    }
    /* 0x4020–0xFFFF: cartridge space */
    if (active_mapper) {
        if (active_mapper->cart &&
            active_mapper->cart->mapper_id == 1 &&
            addr >= 0x8000) {
            /* MMC1 quirk: ignore additional writes from the same CPU instruction.
               This filters RMW double-writes that otherwise corrupt serial load. */
            if (last_mmc1_write_instruction_id == current_instruction_id) {
                return;
            }
            last_mmc1_write_instruction_id = current_instruction_id;
        }
        mapper_prg_write(active_mapper, addr, data);
        return;
    }
    /* No mapper: IRQ vector fallback for test compatibility */
    if (addr == 0xFFFE) { irq_vector_fallback[0] = data; return; }
    if (addr == 0xFFFF) { irq_vector_fallback[1] = data; return; }
}

/* Called once per CPU-rate clock when dma_transfer is active.
   system_clock is the global system clock counter (used for odd/even alignment).
   Returns 1 while DMA is still in progress, 0 when complete. */
int bus_dma_tick(uint64_t system_clock) {
    if (dma_dummy) {
        /* Wait for an odd CPU-rate clock to align (matches real hardware) */
        if (system_clock % 2 == 1)
            dma_dummy = 0;
        return 1;
    }
    if (system_clock % 2 == 0) {
        /* Even: read one byte from CPU bus */
        dma_data = bus_read((Word)dma_page << 8 | dma_addr);
    } else {
        /* Odd: write it to PPU OAM */
        if (active_ppu) {
            /* OAM DMA starts at current OAMADDR and wraps at 256 bytes. */
            Byte oam_index = (Byte)(active_ppu->oam_addr + dma_addr);
            active_ppu->oam[oam_index] = dma_data;
        }
        dma_addr++;
        if (dma_addr == 0x00) {
            /* All 256 bytes written — DMA complete */
            dma_transfer = 0;
            dma_dummy    = 1;
            return 0;
        }
    }
    return 1;
}

int bus_dma_active(void) {
    return dma_transfer;
}