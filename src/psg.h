#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define PSG_REGISTER_COUNT 16

typedef enum {
    PSG_VARIANT_AY8910 = 0,
    PSG_VARIANT_YM2149
} PsgVariant;

typedef struct {
    PsgVariant variant;
    u8 registers[PSG_REGISTER_COUNT];
    u8 selected;

    u16 tone_counter[3];
    bool tone_output[3];

    u16 noise_counter;
    u32 noise_lfsr;

    u32 envelope_counter;
    int envelope_step;
    u8 envelope_attack;
    bool envelope_hold;
    bool envelope_alternate;
    bool envelope_holding;

    u64 clock_fraction;
    s32 highpass_input;
    s32 highpass_output;
    s32 lowpass_output;

    int volume;
    int fixed_levels[16];
    int ym_envelope_levels[32];
} Psg;

void psg_init(Psg *psg, PsgVariant variant);
void psg_reset(Psg *psg);
void psg_set_variant(Psg *psg, PsgVariant variant);
void psg_set_volume(Psg *psg, int volume);

void psg_select(Psg *psg, u8 reg);
void psg_write_data(Psg *psg, u8 value);
void psg_write_register(Psg *psg, unsigned reg, u8 value);
u8   psg_read_data(const Psg *psg);
u8   psg_read_register(const Psg *psg, unsigned reg);

unsigned psg_envelope_level(const Psg *psg);
void psg_advance_ticks(Psg *psg, unsigned ticks);
void psg_render(Psg *psg, s16 *samples, size_t count,
                unsigned clock_hz, unsigned sample_rate);
