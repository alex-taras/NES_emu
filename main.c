#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include "types.h"
#include "cpu.h"
#include "bus.h"
#include "cartridge.h"
#include "mapper.h"
#include "ppu.h"
#include "controller.h"
#include "apu.h"

/* SDL audio callback */
static void apu_sdl_callback(void *userdata, Uint8 *stream, int len) {
    APU *apu = (APU *)userdata;
    int n = len / sizeof(int16_t);
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < n; i++) {
        out[i] = apu_pop_sample(apu);
    }
}

int main(int argc, char **argv) {
    CPU cpu;
    Controller ctrl1;
    controller_reset(&ctrl1);

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
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        SDL_Window *window = SDL_CreateWindow("NES", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 512, 480, 0);
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED);
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, 256, 240);

        PPU ppu;
        ppu_init(&ppu, m);
        bus_connect_ppu(&ppu);
        bus_connect_controllers(&ctrl1, NULL); /* controller 2 not wired */

        /* Initialize APU */
        APU apu;
        apu_init(&apu);
        apu_reset(&apu);
        bus_connect_apu(&apu);

        /* Setup SDL audio */
        SDL_AudioSpec want = {0};
        want.freq     = APU_SAMPLE_RATE;
        want.format   = AUDIO_S16SYS;
        want.channels = 1;
        want.samples  = 512;
        want.callback = apu_sdl_callback;
        want.userdata = &apu;
        SDL_AudioSpec got;
        SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
        SDL_PauseAudioDevice(audio_dev, 0);

        cpu_reset(&cpu);

        int running = 1;
        SDL_Event event;

        /* NES NTSC: 60.0988 Hz → ~16639 µs per frame */
        const Uint64 FRAME_TICKS_US = 16639;
        Uint64 perf_freq = SDL_GetPerformanceFrequency();

        /* New tick-loop architecture */
        uint64_t system_clock = 0;

        while (running) {
            Uint64 frame_start = SDL_GetPerformanceCounter();
            /* SDL event polling */
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = 0;
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = 0;
            }

            /* Build button state from current keyboard snapshot */
            {
                const Uint8 *keys = SDL_GetKeyboardState(NULL);
                uint8_t buttons = 0;
                if (keys[SDL_SCANCODE_Q])     buttons |= BTN_A;
                if (keys[SDL_SCANCODE_W])     buttons |= BTN_B;
                if (keys[SDL_SCANCODE_T])     buttons |= BTN_SELECT;
                if (keys[SDL_SCANCODE_S])     buttons |= BTN_START;
                if (keys[SDL_SCANCODE_UP])    buttons |= BTN_UP;
                if (keys[SDL_SCANCODE_DOWN])  buttons |= BTN_DOWN;
                if (keys[SDL_SCANCODE_LEFT])  buttons |= BTN_LEFT;
                if (keys[SDL_SCANCODE_RIGHT]) buttons |= BTN_RIGHT;
                controller_set_state(&ctrl1, buttons);
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

                /* 3. CPU/DMA and APU run at 1/3 the rate */
                if (system_clock % 3 == 0) {
                    apu_tick(&apu);  /* APU always ticks at CPU rate, even during DMA */
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

            /* Throttle to NES frame rate */
            Uint64 frame_end = SDL_GetPerformanceCounter();
            Uint64 elapsed_us = (frame_end - frame_start) * 1000000 / perf_freq;
            if (elapsed_us < FRAME_TICKS_US) {
                SDL_Delay((Uint32)((FRAME_TICKS_US - elapsed_us) / 1000));
            }
        }

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_CloseAudioDevice(audio_dev);
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
