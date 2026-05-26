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
    void (*destroy)  (Mapper *m);
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

#endif