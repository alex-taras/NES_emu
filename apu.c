#include "apu.h"
#include <string.h>

/* Length counter table (common to all channels except triangle linear counter) */
static const uint8_t LENGTH_TABLE[32] = {
    10,254, 20,  2, 40,  4, 80,  6,160,  8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,192, 24, 72, 26, 16, 28, 32, 30
};

/* Noise period table (NTSC, CPU clocks per timer tick) */
static const uint16_t NOISE_PERIOD_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

/* Duty cycle waveforms (bit sequence, period = 8) */
static const uint8_t DUTY_TABLE[4][8] = {
    {0,1,0,0,0,0,0,0},  /* 12.5% */
    {0,1,1,0,0,0,0,0},  /* 25% */
    {0,1,1,1,1,0,0,0},  /* 50% */
    {1,0,0,1,1,1,1,1},  /* 75% inverted */
};

/* Triangle waveform (32 steps) */
static const uint8_t TRI_TABLE[32] = {
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
     0, 1, 2, 3, 4, 5,6,7,8,9,10,11,12,13,14,15
};

void apu_init(APU *apu) {
    memset(apu, 0, sizeof(APU));
    apu->noise.lfsr = 1;        /* LFSR must start non-zero */
    apu->pulse[0].sweep.ones_complement = 1; /* pulse 1 uses ones' complement negation */
    apu->sample_acc = 0;
    apu->ring_head  = 0;
    apu->ring_tail  = 0;
}

void apu_reset(APU *apu) {
    apu_init(apu);
}

/* Pulse channel helper functions */
static void pulse_tick(PulseChannel *p) {
    if (p->timer == 0) {
        p->timer = p->timer_reload;
        p->duty_index = (p->duty_index + 1) & 7;
    } else {
        p->timer--;
    }
}

static void pulse_sweep_tick(PulseChannel *p) {
    if (p->sweep.reload) {
        p->sweep.divider = p->sweep.period;
        p->sweep.reload  = 0;
        return;
    }
    if (p->sweep.divider > 0) {
        p->sweep.divider--;
        return;
    }
    p->sweep.divider = p->sweep.period;
    if (!p->sweep.enabled || p->sweep.shift == 0) return;
    int delta = p->timer_reload >> p->sweep.shift;
    if (p->sweep.negate)
        delta = p->sweep.ones_complement ? -(delta + 1) : -delta;
    int target = (int)p->timer_reload + delta;
    if (target >= 0 && target <= 0x7FF)
        p->timer_reload = (uint16_t)target;
}

static void pulse_length_tick(PulseChannel *p) {
    if (!p->length_halt && p->length > 0)
        p->length--;
}

static int pulse_output(PulseChannel *p) {
    if (!p->enabled || p->length == 0) return 0;
    if (p->timer_reload < 8) return 0;       /* sweep mute */
    if (!DUTY_TABLE[p->duty][p->duty_index]) return 0;
    return p->env.constant ? p->env.period : p->env.volume;
}

/* Triangle channel helper functions */
static void triangle_tick(TriangleChannel *t) {
    if (t->timer == 0) {
        t->timer = t->timer_reload;
        if (t->length > 0 && t->linear_counter > 0)
            t->step = (t->step + 1) & 31;
    } else {
        t->timer--;
    }
}

static void triangle_length_tick(TriangleChannel *t) {
    if (!t->length_halt && t->length > 0)
        t->length--;
}

static void triangle_linear_tick(TriangleChannel *t) {
    if (t->linear_reload_flag)
        t->linear_counter = t->linear_reload_value;
    else if (t->linear_counter > 0)
        t->linear_counter--;
    if (!t->length_halt)
        t->linear_reload_flag = 0;
}

static int triangle_output(TriangleChannel *t) {
    if (!t->enabled || t->length == 0 || t->linear_counter == 0) return 0;
    return TRI_TABLE[t->step];
}

/* Noise channel helper functions */
static void noise_tick(NoiseChannel *n) {
    if (n->timer == 0) {
        n->timer = n->timer_reload;
        int bit = (n->mode) ? ((n->lfsr >> 6) & 1) : ((n->lfsr >> 1) & 1);
        int feedback = (n->lfsr & 1) ^ bit;
        n->lfsr >>= 1;
        n->lfsr |= (feedback << 14);
    } else {
        n->timer--;
    }
}

static void noise_length_tick(NoiseChannel *n) {
    if (!n->length_halt && n->length > 0)
        n->length--;
}

static int noise_output(NoiseChannel *n) {
    if (!n->enabled || n->length == 0) return 0;
    if (n->lfsr & 1) return 0;               /* bit 0 set = silence */
    return n->env.constant ? n->env.period : n->env.volume;
}

/* Envelope helper functions */
static void envelope_tick(Envelope *e) {
    if (e->start_flag) {
        e->start_flag = 0;
        e->volume  = 15;
        e->divider = e->period;
        return;
    }
    if (e->divider == 0) {
        e->divider = e->period;
        if (e->volume > 0)      e->volume--;
        else if (e->loop)       e->volume = 15;
    } else {
        e->divider--;
    }
}

/* Frame counter helper functions */
static const uint32_t FC_STEPS_4[4] = {7457, 14913, 22371, 29830};
static const uint32_t FC_STEPS_5[5] = {7457, 14913, 22371, 29829, 37281};

static void frame_counter_tick(APU *apu) {
    apu->fc.cycles++;
    int clocked = 0;
    if (apu->fc.mode == 0) {
        for (int i = 0; i < 4; i++) {
            if (apu->fc.cycles == FC_STEPS_4[i]) {
                /* Clock quarter frame */
                envelope_tick(&apu->pulse[0].env);
                envelope_tick(&apu->pulse[1].env);
                envelope_tick(&apu->noise.env);
                triangle_linear_tick(&apu->triangle);

                /* Clock half frame */
                if (i == 1 || i == 3) {
                    pulse_length_tick(&apu->pulse[0]);
                    pulse_length_tick(&apu->pulse[1]);
                    triangle_length_tick(&apu->triangle);
                    noise_length_tick(&apu->noise);
                    pulse_sweep_tick(&apu->pulse[0]);
                    pulse_sweep_tick(&apu->pulse[1]);
                }

                if (i == 3 && !apu->fc.irq_inhibit)
                    apu->fc.irq_flag = 1;

                if (i == 3)
                    apu->fc.cycles = 0;
                clocked = 1;
                break;
            }
        }
    } else {
        for (int i = 0; i < 5; i++) {
            if (apu->fc.cycles == FC_STEPS_5[i]) {
                /* Clock quarter frame */
                envelope_tick(&apu->pulse[0].env);
                envelope_tick(&apu->pulse[1].env);
                envelope_tick(&apu->noise.env);
                triangle_linear_tick(&apu->triangle);

                /* Clock half frame */
                if (i == 1 || i == 4) {
                    pulse_length_tick(&apu->pulse[0]);
                    pulse_length_tick(&apu->pulse[1]);
                    triangle_length_tick(&apu->triangle);
                    noise_length_tick(&apu->noise);
                    pulse_sweep_tick(&apu->pulse[0]);
                    pulse_sweep_tick(&apu->pulse[1]);
                }

                if (i == 4)
                    apu->fc.cycles = 0;
                clocked = 1;
                break;
            }
        }
    }
}

/* Mixing function */
static int16_t mix_output(APU *apu) {
    float p1  = (float)pulse_output(&apu->pulse[0]);
    float p2  = (float)pulse_output(&apu->pulse[1]);
    float tri = (float)triangle_output(&apu->triangle);
    float noi = (float)noise_output(&apu->noise);

    float pulse_out = (p1 + p2 > 0.0f)
        ? 95.88f / (8128.0f / (p1 + p2) + 100.0f)
        : 0.0f;
    float tnd_out = (tri + noi > 0.0f)
        ? 159.79f / (1.0f / (tri / 8227.0f + noi / 12241.0f) + 100.0f)
        : 0.0f;

    float output = pulse_out + tnd_out;   /* 0.0 – ~1.0 */
    return (int16_t)(output * 32767.0f);
}

/* Ring buffer functions */
static void apu_push_sample(APU *apu, int16_t sample) {
    uint32_t next = (apu->ring_head + 1) & (APU_RING_SIZE - 1);
    if (next != apu->ring_tail) {           /* drop if full */
        apu->ring[apu->ring_head] = sample;
        apu->ring_head = next;
    }
}

int16_t apu_pop_sample(APU *apu) {
    if (apu->ring_tail == apu->ring_head) return 0; /* underrun: silence */
    int16_t s = apu->ring[apu->ring_tail];
    apu->ring_tail = (apu->ring_tail + 1) & (APU_RING_SIZE - 1);
    return s;
}

void apu_tick(APU *apu) {
    /* 1. Tick oscillator timers */
    pulse_tick(&apu->pulse[0]);
    pulse_tick(&apu->pulse[1]);
    triangle_tick(&apu->triangle);
    noise_tick(&apu->noise);

    /* 2. Frame counter sequencer */
    frame_counter_tick(apu);

    /* 3. Downsampling */
    apu->sample_acc += APU_SAMPLE_RATE;
    if (apu->sample_acc >= APU_CPU_CLOCK) {
        apu->sample_acc -= APU_CPU_CLOCK;
        float raw = (float)mix_output(apu);

        /* NES analog filter chain (one-pole IIR):
           HP ~37Hz  : capacitor on cartridge output
           HP ~440Hz : inside APU
           LP ~14kHz : cartridge connector RC */
        float hp1_out = 0.99476f * (apu->filter_hp1_prev_out
                        + raw - apu->filter_hp1_prev_in);
        apu->filter_hp1_prev_in  = raw;
        apu->filter_hp1_prev_out = hp1_out;

        float hp2_out = 0.94156f * (apu->filter_hp2_prev_out
                        + hp1_out - apu->filter_hp2_prev_in);
        apu->filter_hp2_prev_in  = hp1_out;
        apu->filter_hp2_prev_out = hp2_out;

        float lp_out = apu->filter_lp_prev
                       + 0.33333f * (hp2_out - apu->filter_lp_prev);
        apu->filter_lp_prev = lp_out;

        apu_push_sample(apu, (int16_t)lp_out);
    }
}

void apu_write(APU *apu, uint16_t addr, uint8_t data) {
    switch (addr) {
        case 0x4000: /* Pulse 1: duty/envelope */
            apu->pulse[0].duty = (data >> 6) & 3;
            apu->pulse[0].length_halt = (data >> 5) & 1;
            apu->pulse[0].env.loop = (data >> 5) & 1;  /* same bit as length_halt */
            apu->pulse[0].env.constant = (data >> 4) & 1;
            apu->pulse[0].env.period = data & 0x0F;
            break;

        case 0x4001: /* Pulse 1: sweep */
            apu->pulse[0].sweep.enabled = (data >> 7) & 1;
            apu->pulse[0].sweep.period = (data >> 4) & 7;
            apu->pulse[0].sweep.negate = (data >> 3) & 1;
            apu->pulse[0].sweep.shift = data & 7;
            apu->pulse[0].sweep.reload = 1;
            break;

        case 0x4002: /* Pulse 1: timer low */
            apu->pulse[0].timer_reload = (apu->pulse[0].timer_reload & 0x700) | data;
            break;

        case 0x4003: /* Pulse 1: timer high + length */
            apu->pulse[0].timer_reload = (apu->pulse[0].timer_reload & 0x0FF) | ((data & 7) << 8);
            apu->pulse[0].length = LENGTH_TABLE[(data >> 3) & 0x1F];
            apu->pulse[0].env.start_flag = 1;
            apu->pulse[0].duty_index = 0;
            break;

        case 0x4004: /* Pulse 2: duty/envelope */
            apu->pulse[1].duty = (data >> 6) & 3;
            apu->pulse[1].length_halt = (data >> 5) & 1;
            apu->pulse[1].env.loop = (data >> 5) & 1;  /* same bit as length_halt */
            apu->pulse[1].env.constant = (data >> 4) & 1;
            apu->pulse[1].env.period = data & 0x0F;
            break;

        case 0x4005: /* Pulse 2: sweep */
            apu->pulse[1].sweep.enabled = (data >> 7) & 1;
            apu->pulse[1].sweep.period = (data >> 4) & 7;
            apu->pulse[1].sweep.negate = (data >> 3) & 1;
            apu->pulse[1].sweep.shift = data & 7;
            apu->pulse[1].sweep.reload = 1;
            break;

        case 0x4006: /* Pulse 2: timer low */
            apu->pulse[1].timer_reload = (apu->pulse[1].timer_reload & 0x700) | data;
            break;

        case 0x4007: /* Pulse 2: timer high + length */
            apu->pulse[1].timer_reload = (apu->pulse[1].timer_reload & 0x0FF) | ((data & 7) << 8);
            apu->pulse[1].length = LENGTH_TABLE[(data >> 3) & 0x1F];
            apu->pulse[1].env.start_flag = 1;
            apu->pulse[1].duty_index = 0;
            break;

        case 0x4008: /* Triangle: linear counter */
            apu->triangle.linear_reload_value = data & 0x7F;
            apu->triangle.length_halt = (data >> 7) & 1;
            break;

        case 0x400A: /* Triangle: timer low */
            apu->triangle.timer_reload = (apu->triangle.timer_reload & 0x700) | data;
            break;

        case 0x400B: /* Triangle: timer high + length */
            apu->triangle.timer_reload = (apu->triangle.timer_reload & 0x0FF) | ((data & 7) << 8);
            apu->triangle.length = LENGTH_TABLE[(data >> 3) & 0x1F];
            apu->triangle.linear_reload_flag = 1;
            break;

        case 0x400C: /* Noise: envelope */
            apu->noise.length_halt = (data >> 5) & 1;
            apu->noise.env.loop = (data >> 5) & 1;  /* same bit as length_halt */
            apu->noise.env.constant = (data >> 4) & 1;
            apu->noise.env.period = data & 0x0F;
            break;

        case 0x400E: /* Noise: mode + period */
            apu->noise.mode = (data >> 7) & 1;
            apu->noise.timer_reload = NOISE_PERIOD_TABLE[data & 0x0F];
            break;

        case 0x400F: /* Noise: length */
            apu->noise.length = LENGTH_TABLE[(data >> 3) & 0x1F];
            apu->noise.env.start_flag = 1;
            break;

        case 0x4010: /* DMC: just ignore */
        case 0x4011:
        case 0x4012:
        case 0x4013:
            break;

        case 0x4015: /* Channel enable */
            apu->pulse[0].enabled = (data & 1) ? 1 : 0;
            apu->pulse[1].enabled = (data & 2) ? 1 : 0;
            apu->triangle.enabled = (data & 4) ? 1 : 0;
            apu->noise.enabled    = (data & 8) ? 1 : 0;
            if (!(data & 1)) apu->pulse[0].length = 0;
            if (!(data & 2)) apu->pulse[1].length = 0;
            if (!(data & 4)) apu->triangle.length = 0;
            if (!(data & 8)) apu->noise.length    = 0;
            break;

        case 0x4017: /* Frame counter mode + IRQ inhibit */
            apu->fc.mode = (data >> 7) & 1;
            apu->fc.irq_inhibit = (data >> 6) & 1;
            apu->fc.cycles = 0;
            if (apu->fc.mode == 1) {
                /* In 5-step mode, immediately clock all units once */
                envelope_tick(&apu->pulse[0].env);
                envelope_tick(&apu->pulse[1].env);
                envelope_tick(&apu->noise.env);
                triangle_linear_tick(&apu->triangle);
                pulse_length_tick(&apu->pulse[0]);
                pulse_length_tick(&apu->pulse[1]);
                triangle_length_tick(&apu->triangle);
                noise_length_tick(&apu->noise);
                pulse_sweep_tick(&apu->pulse[0]);
                pulse_sweep_tick(&apu->pulse[1]);
            }
            if (apu->fc.irq_inhibit)
                apu->fc.irq_flag = 0;
            break;
    }
}

uint8_t apu_read(APU *apu, uint16_t addr) {
    switch (addr) {
        case 0x4015: {
            uint8_t val = ((apu->pulse[0].length > 0) ? 1 : 0) |
                   ((apu->pulse[1].length > 0) ? 2 : 0) |
                   ((apu->triangle.length > 0) ? 4 : 0) |
                   ((apu->noise.length > 0) ? 8 : 0) |
                   ((apu->fc.irq_flag) ? 0x40 : 0);
            apu->fc.irq_flag = 0;  /* Reading $4015 clears frame IRQ flag */
            return val;
        }
        default:
            return 0;
    }
}

int apu_irq(APU *apu) {
    return apu->fc.irq_flag;
}