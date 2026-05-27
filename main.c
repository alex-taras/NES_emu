#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include "types.h"
#include "cpu.h"
#include "bus.h"
#include "cartridge.h"
#include "mapper.h"
#include "ppu.h"

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

        /* SDL init */
        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window *window = SDL_CreateWindow("NES", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 512, 480, 0);
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, 256, 240);

        PPU ppu;
        ppu_init(&ppu, m);
        bus_connect_ppu(&ppu);
        cpu_reset(&cpu);

        int running = 1;
        SDL_Event event;

        /* New tick-loop architecture */
        uint64_t system_clock = 0;

        while (running) {
            /* SDL event polling */
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = 0;
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = 0;
            }

            /* Run until one full frame is complete */
            while (!ppu_frame_complete(&ppu)) {
                /* 1. Tick the PPU every system clock */
                ppu_tick(&ppu);

                /* 2. NMI propagation — check after every PPU dot */
                if (ppu.nmi_output) {
                    ppu.nmi_output  = 0;
                    cpu.nmi_pending = 1;
                }

                /* 3. CPU/DMA runs at 1/3 the rate */
                if (system_clock % 3 == 0) {
                    if (bus_dma_active()) {
                        bus_dma_tick(system_clock);
                    } else if (cpu.cycles_remaining > 0) {
                        cpu.cycles_remaining--;
                    } else {
                        cpu_step(&cpu);
                        cpu.cycles_remaining = cpu.cycles - 1;
                    }
                }

                system_clock++;
            }

            /* Blit framebuffer */
            SDL_UpdateTexture(texture, NULL, ppu.framebuffer, 256 * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        }

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

        bus_set_mapper(NULL);
        bus_connect_ppu(NULL);
        mapper_destroy(m);
        cartridge_free(cart);
    } else {
        cpu_reset(&cpu);
    }

    return 0;
}
