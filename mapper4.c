#include <stdlib.h>
#include <string.h>

#include "mapper4.h"

#define MMC3_PRG_RAM_SIZE 0x2000
#define MMC3_CHR_RAM_SIZE 0x2000

/* PPU mirror mode values used by ppu.c */
#define MIRROR_HORIZONTAL 0
#define MIRROR_VERTICAL   1

typedef struct {
    Mapper base;

    /* Bank select registers */
    Byte bank_select;
    Byte bank_regs[8];
    Byte prg_mode;
    Byte chr_mode;

    /* Mirroring from $A000 */
    Byte mirror_mode;

    /* PRG banks */
    size_t prg_bank_count_8k;
    size_t prg_offsets[4]; /* $8000, $A000, $C000, $E000 */

    /* CHR banks */
    size_t chr_bank_count_1k; /* zero when CHR RAM cart */
    size_t chr_offsets[8];    /* eight 1KB slots */
    Byte chr_ram[MMC3_CHR_RAM_SIZE];

    /* PRG RAM */
    Byte prg_ram[MMC3_PRG_RAM_SIZE];
    Byte prg_ram_enable;
    Byte prg_ram_write_protect;

    /* IRQ */
    Byte irq_latch;
    Byte irq_counter;
    Byte irq_reload;
    Byte irq_enable;
    Byte irq_pending;

    /* A12/scanline clock state placeholder */
    Byte last_a12;
    int a12_low_ppu_cycles;
} Mapper4;

static void m4_update_prg_banks(Mapper4 *m);
static void m4_update_chr_banks(Mapper4 *m);

static size_t m4_prg_bank_index(Mapper4 *m, size_t raw_bank) {
    return raw_bank % m->prg_bank_count_8k;
}

static size_t m4_chr_bank_count_effective(Mapper4 *m) {
    return (m->chr_bank_count_1k > 0) ? m->chr_bank_count_1k : 8;
}

static size_t m4_chr_offset_for_bank(Mapper4 *m, size_t raw_bank) {
    size_t bank_count = m4_chr_bank_count_effective(m);
    return (raw_bank % bank_count) * 0x400;
}

static void m4_set_chr_slot(Mapper4 *m, int slot, size_t raw_bank) {
    m->chr_offsets[slot] = m4_chr_offset_for_bank(m, raw_bank);
}

static Byte m4_prg_read(Mapper *base, Word addr) {
    Mapper4 *m = (Mapper4 *)base;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        return m->prg_ram[addr - 0x6000];
    }

    if (addr >= 0x8000) {
        size_t slot = (addr - 0x8000) >> 13; /* 8KB slot 0..3 */
        size_t offset = m->prg_offsets[slot] + (addr & 0x1FFF);
        return m->base.cart->prg_rom[offset % m->base.cart->prg_size];
    }

    return 0x00;
}

static void m4_prg_write(Mapper *base, Word addr, Byte data) {
    Mapper4 *m = (Mapper4 *)base;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (m->prg_ram_enable && !m->prg_ram_write_protect) {
            m->prg_ram[addr - 0x6000] = data;
        }
        return;
    }

    if (addr < 0x8000) {
        return;
    }

    switch (addr & 0xE001) {
        case 0x8000:
            m->bank_select = data;
            m->prg_mode = (data >> 6) & 0x01;
            m->chr_mode = (data >> 7) & 0x01;
            m4_update_prg_banks(m);
            m4_update_chr_banks(m);
            break;

        case 0x8001: {
            int reg = m->bank_select & 0x07;
            m->bank_regs[reg] = data;
            m4_update_prg_banks(m);
            m4_update_chr_banks(m);
            break;
        }

        case 0xA000:
            /* MMC3: 0 = vertical, 1 = horizontal */
            m->mirror_mode = (data & 0x01) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
            break;

        case 0xA001:
            /* MMC3 PRG RAM protect register: bit7 enable, bit6 write-protect */
            m->prg_ram_enable = (data >> 7) & 0x01;
            m->prg_ram_write_protect = (data >> 6) & 0x01;
            break;

        case 0xC000:
            m->irq_latch = data;
            break;

        case 0xC001:
            m->irq_reload = 1;
            m->irq_counter = 0;
            break;

        case 0xE000:
            m->irq_enable = 0;
            m->irq_pending = 0;
            break;

        case 0xE001:
            m->irq_enable = 1;
            break;

        default:
            break;
    }
}

static Byte m4_chr_read(Mapper *base, Word addr) {
    Mapper4 *m = (Mapper4 *)base;

    if (addr > 0x1FFF) {
        return 0x00;
    }

    size_t slot = addr >> 10;
    size_t offset = m->chr_offsets[slot] + (addr & 0x03FF);
    if (m->chr_bank_count_1k > 0) {
        return m->base.cart->chr_rom[offset % m->base.cart->chr_size];
    }
    return m->chr_ram[offset & 0x1FFF];
}

static void m4_chr_write(Mapper *base, Word addr, Byte data) {
    Mapper4 *m = (Mapper4 *)base;

    if (addr > 0x1FFF || m->chr_bank_count_1k > 0) {
        return;
    }

    size_t slot = addr >> 10;
    size_t offset = m->chr_offsets[slot] + (addr & 0x03FF);
    m->chr_ram[offset & 0x1FFF] = data;
}

static Byte m4_get_mirroring(Mapper *base) {
    Mapper4 *m = (Mapper4 *)base;
    return m->mirror_mode;
}

static void m4_ppu_a12_tick(Mapper *base, Word ppu_addr) {
    Mapper4 *m = (Mapper4 *)base;
    (void)ppu_addr;

    /* Called once per visible scanline from ppu_tick (dot ~260). */
    if (m->irq_counter == 0 || m->irq_reload) {
        m->irq_counter = m->irq_latch;
        m->irq_reload = 0;
    } else {
        m->irq_counter--;
    }

    if (m->irq_counter == 0 && m->irq_enable) {
        m->irq_pending = 1;
    }
}

static Byte m4_irq_pending(Mapper *base) {
    Mapper4 *m = (Mapper4 *)base;
    return m->irq_pending;
}

static void m4_irq_ack(Mapper *base) {
    Mapper4 *m = (Mapper4 *)base;
    m->irq_pending = 0;
}

static void m4_destroy(Mapper *base) {
    free(base);
}

static const MapperOps MAPPER4_OPS = {
    .prg_read = m4_prg_read,
    .prg_write = m4_prg_write,
    .chr_read = m4_chr_read,
    .chr_write = m4_chr_write,
    .get_mirroring = m4_get_mirroring,
    .destroy = m4_destroy,
    .ppu_a12_tick = m4_ppu_a12_tick,
    .irq_pending = m4_irq_pending,
    .irq_ack = m4_irq_ack
};

static void m4_update_prg_banks(Mapper4 *m) {
    size_t last = m->prg_bank_count_8k - 1;
    size_t last2 = m->prg_bank_count_8k - 2;
    size_t b6 = m4_prg_bank_index(m, m->bank_regs[6] & 0x3F);
    size_t b7 = m4_prg_bank_index(m, m->bank_regs[7] & 0x3F);

    if (!m->prg_mode) {
        m->prg_offsets[0] = b6 * 0x2000;
        m->prg_offsets[1] = b7 * 0x2000;
        m->prg_offsets[2] = last2 * 0x2000;
    } else {
        m->prg_offsets[0] = last2 * 0x2000;
        m->prg_offsets[1] = b7 * 0x2000;
        m->prg_offsets[2] = b6 * 0x2000;
    }
    m->prg_offsets[3] = last * 0x2000;
}

static void m4_update_chr_banks(Mapper4 *m) {
    size_t r0 = m->bank_regs[0] & 0xFE;
    size_t r1 = m->bank_regs[1] & 0xFE;
    size_t r2 = m->bank_regs[2];
    size_t r3 = m->bank_regs[3];
    size_t r4 = m->bank_regs[4];
    size_t r5 = m->bank_regs[5];

    if (!m->chr_mode) {
        /* $0000-$07FF=R0 pair, $0800-$0FFF=R1 pair, upper half R2..R5 */
        m4_set_chr_slot(m, 0, r0);
        m4_set_chr_slot(m, 1, r0 + 1);
        m4_set_chr_slot(m, 2, r1);
        m4_set_chr_slot(m, 3, r1 + 1);
        m4_set_chr_slot(m, 4, r2);
        m4_set_chr_slot(m, 5, r3);
        m4_set_chr_slot(m, 6, r4);
        m4_set_chr_slot(m, 7, r5);
    } else {
        /* Mode 1 swaps halves: lower half R2..R5, upper half R0/R1 pairs */
        m4_set_chr_slot(m, 0, r2);
        m4_set_chr_slot(m, 1, r3);
        m4_set_chr_slot(m, 2, r4);
        m4_set_chr_slot(m, 3, r5);
        m4_set_chr_slot(m, 4, r0);
        m4_set_chr_slot(m, 5, r0 + 1);
        m4_set_chr_slot(m, 6, r1);
        m4_set_chr_slot(m, 7, r1 + 1);
    }
}

Mapper *mapper4_create(Cartridge *cart) {
    Mapper4 *m = (Mapper4 *)calloc(1, sizeof(Mapper4));
    if (!m) {
        return NULL;
    }

    m->prg_bank_count_8k = cart->prg_size / 0x2000;
    if (m->prg_bank_count_8k < 2) {
        free(m);
        return NULL;
    }

    m->chr_bank_count_1k = cart->chr_size / 0x400;
    m->base.ops = &MAPPER4_OPS;
    m->base.cart = cart;

    m->mirror_mode = cart->mirroring ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    m->prg_ram_enable = 1;
    m->prg_ram_write_protect = 0;

    m4_update_prg_banks(m);
    m4_update_chr_banks(m);

    return (Mapper *)m;
}
