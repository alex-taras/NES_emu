#include <stdlib.h>
#include "mapper.h"
#include "mapper0.h"
#include "mapper1.h"

Mapper *mapper_create(Cartridge *cart) {
    switch (cart->mapper_id) {
        case 0: return mapper0_create(cart);
        case 1: return mapper1_create(cart);
        default: return NULL;   /* unsupported mapper */
    }
}

void mapper_destroy(Mapper *m) {
    if (!m) return;
    m->ops->destroy(m);
}