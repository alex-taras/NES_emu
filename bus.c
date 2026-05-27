#include "bus.h"
#include "memory.h"
#include "controller.h"

static Mapper *active_mapper = NULL;
static PPU *active_ppu = NULL;
static Controller *ctrl1 = NULL;
static Controller *ctrl2 = NULL;

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

Byte bus_read(Word addr) {
    if (addr <= 0x1FFF) {
        return mem_read(addr & 0x07FF);
    }
    if (addr <= 0x3FFF) {
        if (active_ppu) return ppu_reg_read(active_ppu, addr & 0x07);
        return 0x00;
    }
    if (addr <= 0x401F) {
        if (addr == 0x4016) return ctrl1 ? controller_read(ctrl1) : 0x00;
        if (addr == 0x4017) return ctrl2 ? controller_read(ctrl2) : 0x00;
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
        if (active_ppu) ppu_reg_write(active_ppu, addr & 0x07, data);
        return;
    }
    if (addr == 0x4014) {
        /* OAM DMA: copy 256 bytes from CPU page $XX00-$XXFF to PPU OAM */
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
    if (addr <= 0x401F) {
        return;   /* APU/IO stub */
    }
    /* 0x4020–0xFFFF: cartridge space */
    if (active_mapper) {
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
        if (active_ppu)
            active_ppu->oam[dma_addr] = dma_data;
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