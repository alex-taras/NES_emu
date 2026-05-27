#ifndef APU_H
#define APU_H

#include "types.h"
#include <stdint.h>

#define APU_SAMPLE_RATE  44100
#define APU_CPU_CLOCK    1789773   /* NTSC */
#define APU_RING_SIZE    4096      /* must be power of 2 */

/* --- Envelope --- */
typedef struct {
    int  start_flag;
    int  loop;
    int  constant;
    int  period;       /* reload value (register bits 3-0) */
    int  divider;
    int  volume;       /* output: 0-15 */
} Envelope;

/* --- Sweep (pulse only) --- */
typedef struct {
    int  enabled;
    int  period;
    int  negate;
    int  shift;
    int  reload;
    int  divider;
    int  ones_complement;   /* 1 for pulse 1, 0 for pulse 2 */
} Sweep;

/* --- Pulse channel --- */
typedef struct {
    int      enabled;
    int      duty;         /* 0-3 */
    int      duty_index;   /* 0-7 current position */
    int      length_halt;
    int      length;       /* counter value */
    uint16_t timer_reload;
    uint16_t timer;
    Envelope env;
    Sweep    sweep;
} PulseChannel;

/* --- Triangle channel --- */
typedef struct {
    int      enabled;
    int      length_halt;
    int      length;
    int      linear_reload_flag;
    int      linear_reload_value;
    int      linear_counter;
    uint16_t timer_reload;
    uint16_t timer;
    int      step;          /* 0-31 current waveform step */
} TriangleChannel;

/* --- Noise channel --- */
typedef struct {
    int      enabled;
    int      mode;          /* 0=normal, 1=loop */
    int      length_halt;
    int      length;
    uint16_t timer_reload;
    uint16_t timer;
    uint16_t lfsr;          /* 15-bit shift register, init to 1 */
    Envelope env;
} NoiseChannel;

/* --- Frame counter --- */
typedef struct {
    int      mode;          /* 0=4-step, 1=5-step */
    int      irq_inhibit;
    int      irq_flag;
    uint32_t cycles;        /* counts CPU cycles since last reset */
} FrameCounter;

/* --- APU --- */
typedef struct {
    PulseChannel    pulse[2];
    TriangleChannel triangle;
    NoiseChannel    noise;
    FrameCounter    fc;

    /* downsampling accumulator */
    uint32_t sample_acc;

    /* analog filter state (NES RC filter chain) */
    float filter_hp1_prev_in;   /* high-pass ~37Hz  */
    float filter_hp1_prev_out;
    float filter_hp2_prev_in;   /* high-pass ~440Hz */
    float filter_hp2_prev_out;
    float filter_lp_prev;       /* low-pass  ~14kHz */

    /* ring buffer */
    int16_t  ring[APU_RING_SIZE];
    volatile uint32_t ring_head;
    volatile uint32_t ring_tail;
} APU;

/* Lifecycle */
void apu_init(APU *apu);
void apu_reset(APU *apu);

/* Called at CPU rate from main tick loop */
void apu_tick(APU *apu);

/* Register access (from bus.c) */
extern void apu_write(APU *apu, uint16_t addr, uint8_t data);
extern uint8_t apu_read(APU *apu, uint16_t addr);

/* Called from SDL audio callback */
extern int16_t apu_pop_sample(APU *apu);

/* IRQ output (poll from main loop like ppu.nmi_output) */
extern int apu_irq(APU *apu);

#endif /* APU_H */