#include <stdlib.h>
#include "mapper.h"
#include "mapper0.h"
#include "mapper1.h"
#include "mapper2.h"
#include "mapper4.h"

Mapper *mapper_create(Cartridge *cart) {
    switch (cart->mapper_id) {
        case 0: return mapper0_create(cart);
        case 1: return mapper1_create(cart);
        case 2: return mapper2_create(cart);
        case 4: return mapper4_create(cart);
        default: return NULL;   /* unsupported mapper */
    }
}

void mapper_destroy(Mapper *m) {
    if (!m) return;
    m->ops->destroy(m);
}