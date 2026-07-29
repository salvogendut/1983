#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define SCC_CHANNELS 5u
#define SCC_WAVE_SIZE 32u

typedef enum {
    SCC_MODE_COMPATIBLE = 0,
    SCC_MODE_PLUS
} SccMode;

typedef struct {
    s8 wave[SCC_CHANNELS][SCC_WAVE_SIZE];
    u16 period[SCC_CHANNELS];
    u16 original_period[SCC_CHANNELS];
    u32 counter[SCC_CHANNELS];
    u8 position[SCC_CHANNELS];
    u8 volume[SCC_CHANNELS];
    u8 enabled;
    u8 deform;
    SccMode mode;
    u64 clock_fraction;
} Scc;

void scc_init(Scc *scc);
void scc_reset(Scc *scc);
void scc_set_mode(Scc *scc, SccMode mode);
u8 scc_read(Scc *scc, u8 address);
void scc_write(Scc *scc, u8 address, u8 value);
void scc_render(Scc *scc, s16 *samples, size_t count,
                unsigned clock_hz, unsigned sample_rate);
