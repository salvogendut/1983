/* serial_timing.h - high-resolution emulated-time + clock-pin core for the
 * RS-232C chips (8251/8254), ported from openMSX's EmuTime/Duration/Clock and
 * src/serial/ClockPin.hh.
 *
 * The MSX RS-232C interface is clocked by an 1.8432 MHz crystal, while the emulated
 * CPU runs at MSX_CPU_HZ (3.579545 MHz, src/msx.h). openMSX expresses every
 * device's time on a single high-resolution EmuTime base so that both clocks are
 * exactly representable. We reproduce that with an integer base of
 * SERIAL_MAIN_FREQ units/second chosen so that both 3579545 Hz and 184.32 kHz
 * divide EXACTLY (their lcm). Every 825 reaction is then an integer tick count,
 * exactly as in openMSX.
 *
 * OpenMSX licensing note: this is a deliberate port of GPL-2.0+ openMSX code
 * into 1983 (GPL-2.0-only). Both share the GPL, so reuse is permitted; the
 * original copyright/authorship of the algorithms is retained here and in
 * serial_8251.c/serial_8254.c.
 */
#pragma once

#include "types.h"

#include <stdbool.h>
#include <stddef.h>

/* Units of the zero-remainder master clock. */
#define SERIAL_MAIN_FREQ       UINT64_C(1319563468800)
#define SERIAL_Z80_CYCLE_TICKS UINT64_C(368640)   /* one Z80 cycle  @ 35700Hz */
#define SERIAL_BAUD_TOTAL      UINT64_C(715909)   /* 1/1.8432MHz serial period */
#define SERIAL_BAUD_HI         UINT64_C(357954)   /* half serial period */

typedef u64 ser_time;
typedef u64 ser_dur;

static inline ser_dur ser_dur_hz(u64 hz) { return SERIAL_MAIN_FREQ / hz; }

/* ClockPin: a periodic or single-level logic pin, ported from
 * src/serial/ClockPin.hh (openMSX). A listener may observe state changes. */
typedef struct SerialClock SerialClock;
typedef void (*SerialClockListener)(void *ctx, SerialClock *clock,
                                    bool posedge);

struct SerialClock {
    void *listener_ctx;
    SerialClockListener listener;
    ser_dur totalDur;
    ser_dur hiDur;
    ser_time reference;
    bool periodic;
    bool status;
};

static inline void serial_clock_init(SerialClock *c) {
    c->listener_ctx = NULL;
    c->listener = NULL;
    c->totalDur = 0;
    c->hiDur = 0;
    c->reference = 0;
    c->periodic = false;
    c->status = false;
}

static inline void serial_clock_set_listener(SerialClock *c, void *ctx,
                                      SerialClockListener cb) {
    c->listener_ctx = ctx;
    c->listener = cb;
}

static inline void serial_clock_set_state(SerialClock *c, bool status, ser_time t) {
    bool posedge = status && !c->status;
    c->status = status;
    c->periodic = false;
    c->reference = t;
    if (c->listener) c->listener(c->listener_ctx, c, posedge);
}

static inline void serial_clock_set_periodic(SerialClock *c, ser_dur total,
                                      ser_dur hi, ser_time t) {
    c->periodic = true;
    c->totalDur = total;
    c->hiDur = hi;
    c->reference = t;
    c->status = hi > 0;
    if (c->listener) c->listener(c->listener_ctx, c, true);
}

static inline bool serial_clock_is_periodic(const SerialClock *c) {
    return c->periodic && c->totalDur != 0;
}

static inline bool serial_clock_get_state(const SerialClock *c, ser_time t) {
    /* In periodic mode phase is a virtual oscillation; treat [t, hi) of each
     * period as active exactly as open-MX CocoonClockModels the wave. */
    if (c->periodic && c->totalDur) {
        ser_dur phase = (t >= c->reference) ? (t - c->reference) : 0;
        phase %= c->totalDur;
        return phase < c->hiDur;
    }
    return c->status;
}

static inline ser_dur serial_clock_get_total(const SerialClock *c) {
    return c->totalDur;
}
static inline ser_dur serial_clock_get_hi(const SerialClock *c) {
    return c->hiDur;
}

/* Number of whole clock periods between two instants (floor), like
 * ClockPin::getTicksBetween(). Guarded against reordered arguments. */
static inline unsigned serial_clock_ticks_between(const SerialClock *c,
                                          ser_time b, ser_time e) {
    if (!serial_clock_is_periodic(c)) return 0;
    ser_time span = (e >= b) ? (e - b) : (b - e);
    if (!span || !c->totalDur) return 0;
    unsigned n = (unsigned)(span / c->totalDur);
    return n;
}

/* Advance a high-resolution EmulTime base by whole Z80 cycles. */
static inline ser_time ser_time_advance(ser_time t, u64 z80_cycles) {
    return t + z80_cycles * SERIAL_Z80_CYCLE_TICKS;
}