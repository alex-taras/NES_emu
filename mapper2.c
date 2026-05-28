#include <stdlib.h>
#include <string.h>
#include "mapper2.h"

typedef struct {
    Mapper base;                /* must be first */
    Byte   bank_select;         /* selected bank for $8000-$BFFF */
    size_t prg_bank_count_16k;  /* cart->prg_size / 0x4000 */
    size_t fixed_bank_offset;   /* (last bank) * 0x4000 */
    Byte   chr_ram[8192];       /* used when cart->chr_size == 0 */
} Mapper2;

/* PRG read: 16KB switchable bank at $8000-$BFFF, fixed last bank at $C000-$FFFF */
static Byte m2_prg_read(Mapper *m, Word addr) {
    Mapper2 *m2 = (Mapper2*)m;
    Cartridge *cart = m->cart;

    if (addr >= 0x8000 && addr <= 0xBFFF) {
        /* Switchable 16KB bank: $8000-$BFFF */
        size_t offset = (m2->bank_select * 0x4000) + (addr - 0x8000);
        return cart->prg_rom[offset % cart->prg_size];
    } else if (addr >= 0xC000 && addr <= 0xFFFF) {
        /* Fixed last 16KB bank: $C000-$FFFF */
        size_t offset = m2->fixed_bank_offset + (addr - 0xC000);
        return cart->prg_rom[offset % cart->prg_size];
    } else if (addr >= 0x6000 && addr <= 0x7FFF) {
        /* PRG RAM area (unused in this mapper implementation) */
        return 0x00;
    }
    return 0x00;
}

/* PRG write: update bank select register */
static void m2_prg_write(Mapper *m, Word addr, Byte data) {
    Mapper2 *m2 = (Mapper2*)m;

    /* Only handle writes to the mapper control region */
    if (addr >= 0x8000 && addr <= 0xFFFF) {
        /* Update bank select (only low bits) */
        m2->bank_select = data % m2->prg_bank_count_16k;
    }
}

/* CHR read: either from CHR ROM or CHR RAM */
static Byte m2_chr_read(Mapper *m, Word addr) {
    Mapper2 *m2 = (Mapper2*)m;
    Cartridge *cart = m->cart;

    if (cart->chr_size == 0) {
        /* CHR RAM: use internal RAM */
        return m2->chr_ram[addr & 0x1FFF];
    } else {
        /* CHR ROM: read from cartridge */
        return cart->chr_rom[addr % cart->chr_size];
    }
}

/* CHR write: only to CHR RAM */
static void m2_chr_write(Mapper *m, Word addr, Byte data) {
    Mapper2 *m2 = (Mapper2*)m;
    Cartridge *cart = m->cart;

    if (cart->chr_size == 0) {
        /* CHR RAM: allow writes */
        m2->chr_ram[addr & 0x1FFF] = data;
    }
    /* CHR ROM: writes ignored */
}

/* Static mirroring from ROM header */
static Byte m2_get_mirroring(Mapper *m) {
    return m->cart->mirroring;
}

/* Cleanup */
static void m2_destroy(Mapper *m) {
    free(m);
}

/* Mapper operations */
static const MapperOps MAPPER2_OPS = {
    .prg_read  = m2_prg_read,
    .prg_write = m2_prg_write,
    .chr_read  = m2_chr_read,
    .chr_write = m2_chr_write,
    .get_mirroring = m2_get_mirroring,
    .destroy   = m2_destroy,
};

Mapper *mapper2_create(Cartridge *cart) {
    Mapper2 *m2 = malloc(sizeof(Mapper2));
    if (!m2) return NULL;
    memset(m2, 0, sizeof(*m2));

    m2->base.ops = &MAPPER2_OPS;
    m2->base.cart = cart;

    m2->prg_bank_count_16k = cart->prg_size / 0x4000;
    if (m2->prg_bank_count_16k == 0) {
        free(m2);
        return NULL;
    }

    m2->fixed_bank_offset = (m2->prg_bank_count_16k - 1) * 0x4000;
    m2->bank_select = 0;

    return (Mapper *)m2;
}