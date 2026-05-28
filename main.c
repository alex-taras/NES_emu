#include <stdio.h>
#include <string.h>
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

    int apu_enabled = 1;
    const char *rom_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-apu") == 0) {
            apu_enabled = 0;
            fprintf(stderr, "APU disabled\n");
        } else if (!rom_path) {
            rom_path = argv[i];
        }
    }

    if (rom_path) {
        Cartridge *cart = cartridge_load(rom_path);
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
        if (apu_enabled) bus_connect_apu(&apu);

        /* Setup SDL audio */
        SDL_AudioDeviceID audio_dev = 0;
        if (apu_enabled) {
            SDL_AudioSpec want = {0};
            want.freq     = APU_SAMPLE_RATE;
            want.format   = AUDIO_S16SYS;
            want.channels = 1;
            want.samples  = 512;
            want.callback = apu_sdl_callback;
            want.userdata = &apu;
            SDL_AudioSpec got;
            audio_dev = SDL_OpenAudioDevice(
                NULL, 0, &want, &got,
                SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE
            );
            if (audio_dev == 0) {
                fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
                fprintf(stderr, "Continuing without audio.\n");
            } else {
                fprintf(stderr, "Audio: wanted %d Hz, got %d Hz (%d samples)\n",
                        want.freq, got.freq, got.samples);
                SDL_PauseAudioDevice(audio_dev, 0);
            }
        }

        cpu_reset(&cpu);

        int running = 1;
        SDL_Event event;

        /* NES NTSC: 60.0988 Hz → ~16639 µs per frame */
        const Uint64 FRAME_TICKS_US = 16639;
        Uint64 perf_freq = SDL_GetPerformanceFrequency();

        /* New tick-loop architecture */
        uint64_t system_clock = 0;
        uint64_t last_frame_sig = 0;
        int same_frame_sig_count = 0;
        int frame_stall_logged = 0;

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
            long cpu_steps_this_frame = 0;
            int watchdog_fired = 0;
            Word pc_ring[8] = {0};
            int ring_idx = 0;
            enum { INS_TRACE_RING = 32 };
            Word ins_pc_ring[INS_TRACE_RING];
            Byte ins_op_ring[INS_TRACE_RING];
            Byte ins_b1_ring[INS_TRACE_RING];
            Byte ins_b2_ring[INS_TRACE_RING];
            Byte ins_a_ring[INS_TRACE_RING];
            Byte ins_x_ring[INS_TRACE_RING];
            Byte ins_y_ring[INS_TRACE_RING];
            Byte ins_p_ring[INS_TRACE_RING];
            Byte ins_sp_ring[INS_TRACE_RING];
            Word ins_v_ring[INS_TRACE_RING];
            Byte ins_status_ring[INS_TRACE_RING];
            int ins_ring_idx = 0;
            int loop80_count = 0;
            long ppu_ticks_this_frame = 0;
            int ppu_watchdog_fired = 0;
            bus_reset_debug_stats();
            while (!ppu_frame_complete(&ppu)) {
                /* 1. Tick the PPU every system clock */
                ppu_tick(&ppu);
                ppu_ticks_this_frame++;
                if (ppu_ticks_this_frame > 2000000 && !ppu_watchdog_fired) {
                    fprintf(stderr,
                            "PPU_WATCHDOG: frame exceeded 2M PPU ticks | sl=%d dot=%d frame=%d status=%02X ctrl=%02X mask=%02X v=%04X t=%04X x=%d w=%d nmi=%d\n",
                            ppu.scanline, ppu.dot, ppu.frame, ppu.status, ppu.ctrl, ppu.mask,
                            ppu.v, ppu.t, ppu.x, ppu.w, ppu.nmi_output);
                    ppu_watchdog_fired = 1;
                }

                /* 2. NMI propagation — check after every PPU dot */
                if (ppu.nmi_output) {
                    ppu.nmi_output  = 0;
                    cpu.nmi_pending = 1;
                }

                /* 3. Mapper IRQ propagation — check after every PPU dot */
                if (mapper_irq_pending(ppu.mapper)) {
                    cpu.irq_pending = 1;
                }

                /* 4. CPU/DMA and APU run at 1/3 the rate */
                if (system_clock % 3 == 0) {
                    if (apu_enabled) apu_tick(&apu);  /* APU ticks at CPU rate */
                    if (bus_dma_active()) {
                        bus_dma_tick(system_clock);
                    } else if (cpu.cycles_remaining > 0) {
                        cpu.cycles_remaining--;
                    } else {
                        /* Frame watchdog: detect hang by counting CPU steps per frame */
                        cpu_steps_this_frame++;
                        
                        pc_ring[ring_idx & 7] = cpu.PC;
                        ring_idx++;

                        {
                            int wi = ins_ring_idx & (INS_TRACE_RING - 1);
                            ins_pc_ring[wi] = cpu.PC;
                            ins_op_ring[wi] = bus_read(cpu.PC);
                            ins_b1_ring[wi] = bus_read(cpu.PC + 1);
                            ins_b2_ring[wi] = bus_read(cpu.PC + 2);
                            ins_a_ring[wi] = cpu.regs.A;
                            ins_x_ring[wi] = cpu.regs.X;
                            ins_y_ring[wi] = cpu.regs.Y;
                            ins_p_ring[wi] = cpu.flags;
                            ins_sp_ring[wi] = cpu.SP;
                            ins_v_ring[wi] = ppu.v;
                            ins_status_ring[wi] = ppu.status;
                            ins_ring_idx++;
                        }

                        if (cpu.PC >= 0x80D9 && cpu.PC <= 0x80F5) {
                            loop80_count++;
                            if (loop80_count <= 64 ||
                                loop80_count == 100 || loop80_count == 250 ||
                                loop80_count == 500 || loop80_count == 1000 ||
                                loop80_count == 5000) {
                                BusDebugStats bus_stats;
                                bus_get_debug_stats(&bus_stats);
                                fprintf(stderr,
                                        "LOOP80: pc=%04X count=%d op=%02X %02X %02X | A=%02X X=%02X Y=%02X P=%02X SP=%02X | ZP[00..07]=%02X %02X %02X %02X %02X %02X %02X %02X | PPU sl=%d dot=%d status=%02X v=%04X t=%04X | $2002 reads=%llu sp0=%llu oamdma=%llu page=%02X\n",
                                        cpu.PC, loop80_count,
                                        bus_read(cpu.PC), bus_read(cpu.PC + 1), bus_read(cpu.PC + 2),
                                        cpu.regs.A, cpu.regs.X, cpu.regs.Y, cpu.flags, cpu.SP,
                                        bus_read(0x0000), bus_read(0x0001), bus_read(0x0002), bus_read(0x0003),
                                        bus_read(0x0004), bus_read(0x0005), bus_read(0x0006), bus_read(0x0007),
                                        ppu.scanline, ppu.dot, ppu.status, ppu.v, ppu.t,
                                        (unsigned long long)bus_stats.ppustatus_reads,
                                        (unsigned long long)bus_stats.ppustatus_sprite0_set_reads,
                                        (unsigned long long)bus_stats.oamdma_starts,
                                        bus_stats.last_oamdma_page);
                            }
                        }

                        if (cpu_steps_this_frame > 200000 && !watchdog_fired) {
                            BusDebugStats bus_stats;
                            bus_get_debug_stats(&bus_stats);
                            fprintf(stderr, "WATCHDOG: frame exceeded 200k CPU steps! PC=0x%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X | PPU sl=%d dot=%d status=%02X ctrl=%02X mask=%02X v=%04X t=%04X\n",
                                    cpu.PC, cpu.regs.A, cpu.regs.X, cpu.regs.Y, cpu.flags, cpu.SP,
                                    ppu.scanline, ppu.dot, ppu.status, ppu.ctrl, ppu.mask, ppu.v, ppu.t);
                            fprintf(stderr, "  Recent PCs: ");
                            for (int i = 0; i < 8; i++)
                                fprintf(stderr, "%04X ", pc_ring[(ring_idx + i) & 7]);
                            fprintf(stderr, "\n");
                            /* Dump sprite 0 OAM entry */
                            fprintf(stderr, "  OAM[0]: Y=%d tile=%02X attr=%02X X=%d | sprite_zero_on_line=%d sprite_count=%d\n",
                                    ppu.oam[0], ppu.oam[1], ppu.oam[2], ppu.oam[3],
                                    ppu.sprite_zero_on_line, ppu.sprite_count);
                            /* Check what BG pixel is at sprite 0's X position on its expected scanline */
                            fprintf(stderr, "  PPU fine_x=%d bg_shift_lo=%04X bg_shift_hi=%04X\n",
                                    ppu.x, ppu.bg_shift_lo, ppu.bg_shift_hi);
                            fprintf(stderr, "  Sprite0 debug: eval_count=%d hit_count=%d\n",
                                    ppu.dbg_sp0_eval_count, ppu.dbg_sp0_hit_count);
                            fprintf(stderr, "  Bus debug: $2002 reads=%llu (vblank=%llu, sp0=%llu, last=%02X) | $4014 starts=%llu last_page=%02X | $2003 writes=%llu last=%02X | $2004=%llu $2005=%llu $2006=%llu $2007=%llu\n",
                                    (unsigned long long)bus_stats.ppustatus_reads,
                                    (unsigned long long)bus_stats.ppustatus_vblank_set_reads,
                                    (unsigned long long)bus_stats.ppustatus_sprite0_set_reads,
                                    bus_stats.last_ppustatus_value,
                                    (unsigned long long)bus_stats.oamdma_starts,
                                    bus_stats.last_oamdma_page,
                                    (unsigned long long)bus_stats.oamaddr_writes,
                                    bus_stats.last_oamaddr_value,
                                    (unsigned long long)bus_stats.oamdata_writes,
                                    (unsigned long long)bus_stats.ppuscroll_writes,
                                    (unsigned long long)bus_stats.ppuaddr_writes,
                                    (unsigned long long)bus_stats.ppudata_writes);
                            fprintf(stderr, "  Recent instructions:\n");
                            for (int i = 0; i < INS_TRACE_RING; i++) {
                                int ri = (ins_ring_idx + i) & (INS_TRACE_RING - 1);
                                fprintf(stderr, "    PC=%04X OP=%02X %02X %02X | A=%02X X=%02X Y=%02X P=%02X SP=%02X | PPU status=%02X v=%04X\n",
                                        ins_pc_ring[ri], ins_op_ring[ri], ins_b1_ring[ri], ins_b2_ring[ri],
                                        ins_a_ring[ri], ins_x_ring[ri], ins_y_ring[ri], ins_p_ring[ri], ins_sp_ring[ri],
                                        ins_status_ring[ri], ins_v_ring[ri]);
                            }
                            fprintf(stderr, "  Loop bytes @80D9: ");
                            for (Word a = 0x80D9; a <= 0x80E6; a++) {
                                fprintf(stderr, "%02X ", bus_read(a));
                            }
                            fprintf(stderr, "\n");
                            watchdog_fired = 1;
                        }
                        cpu_step(&cpu);
                        cpu.cycles_remaining = cpu.cycles - 1;
                    }
                }

                system_clock++;
            }

            {
                uint64_t sig = 1469598103934665603ULL;
                for (int i = 0; i < 64; i++) {
                    int x = (i * 37) & 255;
                    int y = (i * 53) % 240;
                    uint32_t px = ppu.framebuffer[y * 256 + x];
                    sig ^= (uint64_t)px + ((uint64_t)x << 8) + ((uint64_t)y << 16);
                    sig *= 1099511628211ULL;
                }
                if (sig == last_frame_sig) {
                    same_frame_sig_count++;
                    if (!frame_stall_logged &&
                        same_frame_sig_count >= 120) {
                        BusDebugStats bus_stats;
                        bus_get_debug_stats(&bus_stats);
                        fprintf(stderr,
                                "FRAME_STALL: same_sig_frames=%d sig=%016llX | CPU PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X | PPU sl=%d dot=%d status=%02X ctrl=%02X mask=%02X v=%04X t=%04X x=%d w=%d frame=%d | $2002 reads=%llu sp0=%llu oamdma=%llu\n",
                                same_frame_sig_count, (unsigned long long)sig,
                                cpu.PC, cpu.regs.A, cpu.regs.X, cpu.regs.Y, cpu.flags, cpu.SP,
                                ppu.scanline, ppu.dot, ppu.status, ppu.ctrl, ppu.mask, ppu.v, ppu.t, ppu.x, ppu.w, ppu.frame,
                                (unsigned long long)bus_stats.ppustatus_reads,
                                (unsigned long long)bus_stats.ppustatus_sprite0_set_reads,
                                (unsigned long long)bus_stats.oamdma_starts);
                        frame_stall_logged = 1;
                    }
                } else {
                    last_frame_sig = sig;
                    same_frame_sig_count = 0;
                    frame_stall_logged = 0;
                }
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
