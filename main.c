#include <stdio.h>
#include "types.h"
#include "cpu.h"
#include "bus.h"
#include "cartridge.h"
#include "mapper.h"

int main(int argc, char **argv) {
    CPU cpu;

    if (argc >= 2) {
        Cartridge *cart = cartridge_load(argv[1]);
        if (!cart) {
            fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
            return 1;
        }
        Mapper *m = mapper_create(cart);
        if (!m) {
            fprintf(stderr, "Unsupported mapper %d\n", cart->mapper_id);
            cartridge_free(cart);
            return 1;
        }
        bus_set_mapper(m);
        cpu_reset(&cpu);
        cpu_execute(30000, &cpu);
        bus_set_mapper(NULL);
        mapper_destroy(m);
        cartridge_free(cart);
    } else {
        cpu_reset(&cpu);
    }

    return 0;
}
