#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cartridge.h"

static const Byte INES_MAGIC[4] = {0x4E, 0x45, 0x53, 0x1A};

Cartridge *cartridge_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    INESHeader hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return NULL; }
    if (memcmp(hdr.magic, INES_MAGIC, 4) != 0)           { fclose(f); return NULL; }

    /* Skip 512-byte trainer if present */
    if (hdr.flags6 & 0x04) fseek(f, 512, SEEK_CUR);

    size_t prg_size = (size_t)hdr.prg_banks * 16 * 1024;
    size_t chr_size = (size_t)hdr.chr_banks * 8  * 1024;

    if (prg_size == 0) { fclose(f); return NULL; }

    Byte *prg = malloc(prg_size);
    if (!prg) { fclose(f); return NULL; }
    if (fread(prg, 1, prg_size, f) != prg_size) { free(prg); fclose(f); return NULL; }

    Byte *chr = NULL;
    if (chr_size > 0) {
        chr = malloc(chr_size);
        if (!chr) { free(prg); fclose(f); return NULL; }
        if (fread(chr, 1, chr_size, f) != chr_size) { free(chr); free(prg); fclose(f); return NULL; }
    }
    fclose(f);

    Cartridge *cart = malloc(sizeof(Cartridge));
    if (!cart) { free(chr); free(prg); return NULL; }

    cart->prg_rom   = prg;
    cart->prg_size  = prg_size;
    cart->chr_rom   = chr;
    cart->chr_size  = chr_size;
    cart->mapper_id = ((hdr.flags6 >> 4) & 0x0F) | (hdr.flags7 & 0xF0);
    cart->mirroring = hdr.flags6 & 0x01;
    cart->has_battery = (hdr.flags6 >> 1) & 0x01;

    return cart;
}

Cartridge *cartridge_create_from_buffer(const Byte *prg, size_t prg_size,
                                        const Byte *chr, size_t chr_size,
                                        int mapper_id, Byte mirroring) {
    if (!prg || prg_size == 0) return NULL;

    Cartridge *cart = malloc(sizeof(Cartridge));
    if (!cart) return NULL;

    cart->prg_rom = malloc(prg_size);
    if (!cart->prg_rom) { free(cart); return NULL; }
    memcpy(cart->prg_rom, prg, prg_size);
    cart->prg_size = prg_size;

    cart->chr_rom = NULL;
    cart->chr_size = 0;
    if (chr && chr_size > 0) {
        cart->chr_rom = malloc(chr_size);
        if (!cart->chr_rom) { free(cart->prg_rom); free(cart); return NULL; }
        memcpy(cart->chr_rom, chr, chr_size);
        cart->chr_size = chr_size;
    }

    cart->mapper_id   = mapper_id;
    cart->mirroring   = mirroring;
    cart->has_battery = 0;
    return cart;
}

void cartridge_free(Cartridge *cart) {
    if (!cart) return;
    free(cart->prg_rom);
    free(cart->chr_rom);
    free(cart);
}