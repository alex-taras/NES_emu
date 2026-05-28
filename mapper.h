#ifndef MAPPER_H
#define MAPPER_H

#include "types.h"
#include "cartridge.h"

typedef struct Mapper Mapper;

typedef struct {
    Byte (*prg_read) (Mapper *m, Word addr);
    void (*prg_write)(Mapper *m, Word addr, Byte data);
    Byte (*chr_read) (Mapper *m, Word addr);
    void (*chr_write)(Mapper *m, Word addr, Byte data);
    Byte (*get_mirroring)(Mapper *m);
    void (*destroy)  (Mapper *m);
    /* Optional hooks for advanced mappers */
    void (*ppu_a12_tick)(Mapper *m, Word ppu_addr);
    Byte (*irq_pending)(Mapper *m);
    void (*irq_ack)(Mapper *m);
} MapperOps;

struct Mapper {
    const MapperOps *ops;
    Cartridge *cart;
    /* mapper-specific state follows in subtype structs */
};

/* Factory: allocates correct mapper subtype based on cart->mapper_id.
   Returns NULL if mapper_id is unsupported. */
Mapper *mapper_create(Cartridge *cart);

void mapper_destroy(Mapper *m);

/* Inline wrappers — call these everywhere instead of ops directly */
static inline Byte mapper_prg_read(Mapper *m, Word addr) {
    return m->ops->prg_read(m, addr);
}
static inline void mapper_prg_write(Mapper *m, Word addr, Byte data) {
    m->ops->prg_write(m, addr, data);
}
static inline Byte mapper_chr_read(Mapper *m, Word addr) {
    return m->ops->chr_read(m, addr);
}
static inline void mapper_chr_write(Mapper *m, Word addr, Byte data) {
    m->ops->chr_write(m, addr, data);
}
static inline Byte mapper_get_mirroring(Mapper *m) {
    if (m->ops->get_mirroring) {
        return m->ops->get_mirroring(m);
    }
    return 0;  /* fallback to default */
}
static inline void mapper_ppu_a12_tick(Mapper *m, Word ppu_addr) {
    if (m && m->ops->ppu_a12_tick) {
        m->ops->ppu_a12_tick(m, ppu_addr);
    }
}
static inline Byte mapper_irq_pending(Mapper *m) {
    if (m && m->ops->irq_pending) {
        return m->ops->irq_pending(m);
    }
    return 0;
}
static inline void mapper_irq_ack(Mapper *m) {
    if (m && m->ops->irq_ack) {
        m->ops->irq_ack(m);
    }
}

#endif