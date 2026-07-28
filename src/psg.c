#include "psg.h"

#include <stdint.h>
#include <string.h>

/*
 * The fixed-volume curve selects the odd steps from the YM2149's 32-level
 * envelope curve, matching the model used by openMSX. Values are scaled so
 * three channels at full volume fit in signed 16-bit output.
 */
static const int fixed_amplitude[16] = {
       0,   85,  121,  171,  241,  341,  483,  683,
     965, 1365, 1931, 2730, 3862, 5461, 7723, 10922,
};

static const int ym_envelope_amplitude[32] = {
       0,   60,   72,   85,  101,  121,  144,  171,
     203,  241,  287,  341,  406,  483,  574,  683,
     812,  965, 1148, 1365, 1624, 1931, 2296, 2730,
    3247, 3862, 4592, 5461, 6494, 7723, 9184, 10922,
};

#define HIGHPASS_Q15 32604

static int clamp_volume(int volume) {
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}

static void rebuild_levels(Psg *psg) {
    int gain = psg->volume * psg->volume;

    for (unsigned i = 0; i < 16; ++i)
        psg->fixed_levels[i] =
            (fixed_amplitude[i] * gain + 5000) / 10000;
    for (unsigned i = 0; i < 32; ++i)
        psg->ym_envelope_levels[i] =
            (ym_envelope_amplitude[i] * gain + 5000) / 10000;
}

static unsigned tone_period(const Psg *psg, unsigned channel) {
    unsigned fine = psg->registers[channel * 2];
    unsigned coarse = psg->registers[channel * 2 + 1] & 0x0f;
    unsigned period = fine | (coarse << 8);

    return period ? period : 1;
}

static unsigned noise_period(const Psg *psg) {
    unsigned period = psg->registers[6] & 0x1f;

    return period ? period : 1;
}

static unsigned envelope_period(const Psg *psg) {
    unsigned period =
        psg->registers[11] | ((unsigned)psg->registers[12] << 8);

    /* On both chips period zero is twice as fast as period one. */
    return period ? period * 8 : 4;
}

static void restart_envelope(Psg *psg, u8 shape) {
    psg->envelope_attack = shape & 0x04 ? 0x1f : 0x00;
    if (!(shape & 0x08)) {
        psg->envelope_hold = true;
        psg->envelope_alternate = psg->envelope_attack != 0;
    } else {
        psg->envelope_hold = (shape & 0x01) != 0;
        psg->envelope_alternate = (shape & 0x02) != 0;
    }
    psg->envelope_counter = 0;
    psg->envelope_step = 0x1f;
    psg->envelope_holding = false;
}

void psg_init(Psg *psg, PsgVariant variant) {
    if (!psg)
        return;
    memset(psg, 0, sizeof(*psg));
    psg->variant = variant;
    psg->volume = 80;
    rebuild_levels(psg);
    psg_reset(psg);
}

void psg_reset(Psg *psg) {
    PsgVariant variant;
    int volume;

    if (!psg)
        return;
    variant = psg->variant;
    volume = clamp_volume(psg->volume);
    memset(psg, 0, sizeof(*psg));
    psg->variant = variant;
    psg->volume = volume;
    psg->noise_lfsr = 1;
    rebuild_levels(psg);
    restart_envelope(psg, 0);
}

void psg_set_variant(Psg *psg, PsgVariant variant) {
    if (!psg)
        return;
    psg->variant = variant == PSG_VARIANT_YM2149
                 ? PSG_VARIANT_YM2149 : PSG_VARIANT_AY8910;
}

void psg_set_volume(Psg *psg, int volume) {
    if (!psg)
        return;
    psg->volume = clamp_volume(volume);
    rebuild_levels(psg);
}

void psg_select(Psg *psg, u8 reg) {
    if (psg)
        psg->selected = reg & 0x0f;
}

void psg_write_register(Psg *psg, unsigned reg, u8 value) {
    if (!psg || reg >= PSG_REGISTER_COUNT)
        return;

    psg->registers[reg] = value;
    if (reg <= 5) {
        unsigned channel = reg / 2;
        unsigned limit = tone_period(psg, channel) * 8;
        if (psg->tone_counter[channel] >= limit)
            psg->tone_counter[channel] = (u16)(limit - 1);
    } else if (reg == 6) {
        unsigned limit = noise_period(psg) * 16;
        if (psg->noise_counter >= limit)
            psg->noise_counter = (u16)(limit - 1);
    } else if (reg == 11 || reg == 12) {
        unsigned limit = envelope_period(psg);
        if (psg->envelope_counter >= limit)
            psg->envelope_counter = limit - 1;
    } else if (reg == 13) {
        restart_envelope(psg, value);
    }
}

void psg_write_data(Psg *psg, u8 value) {
    if (psg)
        psg_write_register(psg, psg->selected, value);
}

u8 psg_read_register(const Psg *psg, unsigned reg) {
    static const u8 ay_read_mask[PSG_REGISTER_COUNT] = {
        0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
        0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff,
    };

    if (!psg || reg >= PSG_REGISTER_COUNT)
        return 0xff;
    return psg->variant == PSG_VARIANT_AY8910
         ? psg->registers[reg] & ay_read_mask[reg]
         : psg->registers[reg];
}

u8 psg_read_data(const Psg *psg) {
    return psg ? psg_read_register(psg, psg->selected) : 0xff;
}

unsigned psg_envelope_level(const Psg *psg) {
    unsigned level;

    if (!psg)
        return 0;
    level = (unsigned)(psg->envelope_step ^ psg->envelope_attack) & 0x1f;
    return psg->variant == PSG_VARIANT_AY8910 ? level / 2 : level;
}

static void advance_envelope(Psg *psg) {
    if (psg->envelope_holding)
        return;
    --psg->envelope_step;
    if (psg->envelope_step >= 0)
        return;

    if (psg->envelope_hold) {
        if (psg->envelope_alternate)
            psg->envelope_attack ^= 0x1f;
        psg->envelope_step = 0;
        psg->envelope_holding = true;
    } else {
        if (psg->envelope_alternate)
            psg->envelope_attack ^= 0x1f;
        psg->envelope_step = 0x1f;
    }
}

static void psg_tick(Psg *psg) {
    for (unsigned channel = 0; channel < 3; ++channel) {
        unsigned period = tone_period(psg, channel) * 8;
        if (++psg->tone_counter[channel] >= period) {
            psg->tone_counter[channel] = 0;
            psg->tone_output[channel] = !psg->tone_output[channel];
        }
    }

    if (++psg->noise_counter >= noise_period(psg) * 16) {
        u32 feedback =
            (psg->noise_lfsr ^ (psg->noise_lfsr >> 3)) & 1;
        psg->noise_counter = 0;
        psg->noise_lfsr =
            (psg->noise_lfsr >> 1) | (feedback << 16);
    }

    if (!psg->envelope_holding &&
        ++psg->envelope_counter >= envelope_period(psg)) {
        psg->envelope_counter = 0;
        advance_envelope(psg);
    }
}

void psg_advance_ticks(Psg *psg, unsigned ticks) {
    if (!psg)
        return;
    while (ticks--)
        psg_tick(psg);
}

static int envelope_amplitude(const Psg *psg) {
    unsigned level =
        (unsigned)(psg->envelope_step ^ psg->envelope_attack) & 0x1f;

    if (psg->variant == PSG_VARIANT_AY8910)
        return psg->fixed_levels[level / 2];
    return psg->ym_envelope_levels[level];
}

static int current_amplitude(const Psg *psg) {
    int mixed = 0;
    int noise = psg->noise_lfsr & 1;
    u8 mixer = psg->registers[7];

    for (unsigned channel = 0; channel < 3; ++channel) {
        bool tone_high =
            (mixer & (1u << channel)) || psg->tone_output[channel];
        bool noise_high =
            (mixer & (1u << (channel + 3))) || noise;
        u8 volume = psg->registers[8 + channel];
        int level = volume & 0x10
                  ? envelope_amplitude(psg)
                  : psg->fixed_levels[volume & 0x0f];

        if (tone_high && noise_high)
            mixed += level;
    }
    return mixed;
}

static s16 filter_sample(Psg *psg, int input) {
    s32 highpass =
        input - psg->highpass_input +
        (s32)(((int64_t)HIGHPASS_Q15 * psg->highpass_output) >> 15);

    psg->highpass_input = input;
    psg->highpass_output = highpass;
    psg->lowpass_output = (highpass + psg->lowpass_output) >> 1;
    if (psg->lowpass_output > 32767)
        return 32767;
    if (psg->lowpass_output < -32768)
        return -32768;
    return (s16)psg->lowpass_output;
}

void psg_render(Psg *psg, s16 *samples, size_t count,
                unsigned clock_hz, unsigned sample_rate) {
    if (!psg || !samples || !clock_hz || !sample_rate)
        return;

    for (size_t sample = 0; sample < count; ++sample) {
        int64_t sum = 0;
        unsigned ticks;

        psg->clock_fraction += clock_hz;
        ticks = (unsigned)(psg->clock_fraction / sample_rate);
        psg->clock_fraction %= sample_rate;
        if (ticks) {
            for (unsigned tick = 0; tick < ticks; ++tick) {
                psg_tick(psg);
                sum += current_amplitude(psg);
            }
            samples[sample] =
                filter_sample(psg, (int)(sum / ticks));
        } else {
            samples[sample] =
                filter_sample(psg, current_amplitude(psg));
        }
    }
}
