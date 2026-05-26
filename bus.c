#include "bus.h"
#include "memory.h"

static Mapper *active_mapper = NULL;

/* Fallback for when no mapper is connected.
   Covers only 0xFFFE–0xFFFF so the BRK test (which writes the IRQ vector
   directly via bus_write) still works without a cartridge. */
static Byte irq_vector_fallback[2];

void bus_reset() {
    mem_reset();
    irq_vector_fallback[0] = 0x00;
    irq_vector_fallback[1] = 0x00;
    /* active_mapper is NOT cleared here — soft reset must keep mapper attached */
}

void bus_set_mapper(Mapper *m) {
    active_mapper = m;
}

Byte bus_read(Word addr) {
    if (addr <= 0x1FFF) {
        return mem_read(addr & 0x07FF);
    }
    if (addr <= 0x3FFF) {
        return 0x00;   /* PPU stub */
    }
    if (addr <= 0x401F) {
        return 0x00;   /* APU/IO stub */
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
        return;   /* PPU stub */
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