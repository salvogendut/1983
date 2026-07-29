#include "scc.h"

#include <string.h>

static void update_period(Scc *scc, unsigned channel) {
    unsigned period = scc->original_period[channel] & 0x0fff;

    if (scc->deform & 0x02)
        period &= 0x00ff;
    else if (scc->deform & 0x01)
        period >>= 8;
    scc->period[channel] = (u16)period;
    scc->counter[channel] = 0;
    if (scc->deform & 0x20)
        scc->position[channel] = 0;
}

void scc_init(Scc *scc) {
    if (!scc)
        return;
    memset(scc, 0, sizeof(*scc));
    memset(scc->wave, 0xff, sizeof(scc->wave));
    for (unsigned channel = 0; channel < SCC_CHANNELS; ++channel)
        scc->volume[channel] = 15;
    scc->mode = SCC_MODE_COMPATIBLE;
    scc_reset(scc);
}

void scc_reset(Scc *scc) {
    if (!scc)
        return;
    scc->enabled = 0;
    scc->deform = 0;
    scc->clock_fraction = 0;
    memset(scc->counter, 0, sizeof(scc->counter));
    memset(scc->position, 0, sizeof(scc->position));
}

void scc_set_mode(Scc *scc, SccMode mode) {
    if (scc)
        scc->mode = mode == SCC_MODE_PLUS
                  ? SCC_MODE_PLUS : SCC_MODE_COMPATIBLE;
}

static u8 read_wave(const Scc *scc, unsigned channel, u8 address) {
    return (u8)scc->wave[channel][address & 0x1f];
}

u8 scc_read(Scc *scc, u8 address) {
    if (!scc)
        return 0xff;
    if (scc->mode == SCC_MODE_PLUS) {
        if (address < 0xa0)
            return read_wave(scc, address >> 5, address);
    } else {
        if (address < 0x80)
            return read_wave(scc, address >> 5, address);
        if (address >= 0xa0 && address < 0xc0)
            return read_wave(scc, 4, address);
    }
    if (address >= 0xc0 && address < 0xe0)
        scc->deform = 0xff;
    return 0xff;
}

static void write_wave(Scc *scc, unsigned channel,
                       u8 address, u8 value) {
    if (channel >= SCC_CHANNELS)
        return;
    scc->wave[channel][address & 0x1f] = (s8)value;
    if (scc->mode == SCC_MODE_COMPATIBLE && channel == 3)
        scc->wave[4][address & 0x1f] = (s8)value;
}

static void write_frequency_volume(Scc *scc, u8 address, u8 value) {
    unsigned reg = address & 0x0f;

    if (reg < 10) {
        unsigned channel = reg >> 1;

        if (reg & 1)
            scc->original_period[channel] =
                (scc->original_period[channel] & 0x00ff) |
                ((u16)(value & 0x0f) << 8);
        else
            scc->original_period[channel] =
                (scc->original_period[channel] & 0x0f00) | value;
        update_period(scc, channel);
    } else if (reg < 15) {
        scc->volume[reg - 10] = value & 0x0f;
    } else {
        scc->enabled = value & 0x1f;
    }
}

void scc_write(Scc *scc, u8 address, u8 value) {
    if (!scc)
        return;
    if (scc->mode == SCC_MODE_PLUS) {
        if (address < 0xa0)
            write_wave(scc, address >> 5, address, value);
        else if (address < 0xc0)
            write_frequency_volume(scc, address, value);
        else if (address < 0xe0) {
            scc->deform = value;
            for (unsigned i = 0; i < SCC_CHANNELS; ++i)
                update_period(scc, i);
        }
    } else {
        if (address < 0x80)
            write_wave(scc, address >> 5, address, value);
        else if (address < 0xa0)
            write_frequency_volume(scc, address, value);
        else if (address >= 0xc0 && address < 0xe0) {
            scc->deform = value;
            for (unsigned i = 0; i < SCC_CHANNELS; ++i)
                update_period(scc, i);
        }
    }
}

static int current_sample(const Scc *scc) {
    int mixed = 0;

    for (unsigned channel = 0; channel < SCC_CHANNELS; ++channel) {
        if (scc->enabled & (1u << channel))
            mixed +=
                ((int)scc->wave[channel][scc->position[channel]] *
                 scc->volume[channel]) >> 4;
    }
    return mixed;
}

static void tick(Scc *scc) {
    for (unsigned channel = 0; channel < SCC_CHANNELS; ++channel) {
        unsigned period = scc->period[channel] + 1u;

        if (scc->period[channel] <= 8)
            continue;
        scc->counter[channel] += 32;
        while (scc->counter[channel] >= period) {
            scc->counter[channel] -= period;
            scc->position[channel] =
                (scc->position[channel] + 1) & 31;
        }
    }
}

void scc_render(Scc *scc, s16 *samples, size_t count,
                unsigned clock_hz, unsigned sample_rate) {
    if (!scc || !samples || !clock_hz || !sample_rate)
        return;
    for (size_t sample = 0; sample < count; ++sample) {
        int sum = 0;
        unsigned ticks;

        scc->clock_fraction += clock_hz / 32u;
        ticks = (unsigned)(scc->clock_fraction / sample_rate);
        scc->clock_fraction %= sample_rate;
        if (!ticks)
            sum = current_sample(scc);
        else {
            for (unsigned i = 0; i < ticks; ++i) {
                tick(scc);
                sum += current_sample(scc);
            }
            sum /= (int)ticks;
        }
        sum *= 8;
        if (sum > 32767)
            sum = 32767;
        else if (sum < -32768)
            sum = -32768;
        samples[sample] = (s16)sum;
    }
}
