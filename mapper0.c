#include <stdlib.h>
#include "mapper0.h"

typedef struct {
    Mapper base;   /* MUST be first — allows casting between Mapper0* and Mapper* */
} Mapper0;

/* PRG address translation: 16KB mirrors or 32KB fills window */
static Byte m0_prg_read(Mapper *m, Word addr) {
    /* PRG window: 0x8000–0xFFFF */
    return m->cart->prg_rom[(addr - 0x8000) % m->cart->prg_size];
}

static void m0_prg_write(Mapper *m, Word addr, Byte data) {
    (void)m; (void)addr; (void)data;
    /* NROM has no writable PRG — writes are ignored */
}

static Byte m0_chr_read(Mapper *m, Word addr) {
    if (!m->cart->chr_rom || m->cart->chr_size == 0) return 0x00;
    return m->cart->chr_rom[addr % m->cart->chr_size];
}

static void m0_chr_write(Mapper *m, Word addr, Byte data) {
    (void)m; (void)addr; (void)data;
    /* NROM CHR is ROM — writes ignored (CHR-RAM variant not handled here) */
}

static Byte m0_get_mirroring(Mapper *m) {
    /* Mapper 0 uses static mirroring from ROM header */
    return m->cart->mirroring;
}

static void m0_destroy(Mapper *m) {
    free(m);
}

static const MapperOps MAPPER0_OPS = {
    .prg_read  = m0_prg_read,
    .prg_write = m0_prg_write,
    .chr_read  = m0_chr_read,
    .chr_write = m0_chr_write,
    .get_mirroring = m0_get_mirroring,
    .destroy   = m0_destroy,
};

Mapper *mapper0_create(Cartridge *cart) {
    Mapper0 *m = malloc(sizeof(Mapper0));
    if (!m) return NULL;
    m->base.ops  = &MAPPER0_OPS;
    m->base.cart = cart;
    return (Mapper *)m;
}