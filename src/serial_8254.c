/* serial_8254.c - 8253/8254 three-channel counter/timer, a C port of
 * openMSX src/serial/I8254.cc (GPL-2.0+). serial_timing.h replaces the
 * openMSX EmuTime/Scheduler plumbing; the register-visible counting, latch
 * and read-back semantics mirror the original exactly.
 */

#include "serial_8254.h"

#include <string.h>

static void counter_reset(Ser8254Counter *c, ser_time now);
static void counter_advance(Ser8254Counter *c, ser_time now);
static void counter_write_load(Ser8254Counter *c, u16 value, ser_time now);
static void counter_latch_status(Ser8254Counter *c, ser_time now);
static void counter_latch_counter(Ser8254Counter *c, ser_time now);
static void counter_write_control_word(Ser8254Counter *c, u8 value,
                                       ser_time now);

void ser8254_init(Ser8254 *p) {
    memset(p, 0, sizeof(*p));
    for (unsigned i = 0; i < SER8254_CHANNELS; ++i) {
        serial_clock_init(&p->channel[i].clock);
        serial_clock_init(&p->channel[i].output);
    }
}

void ser8254_reset(Ser8254 *p, ser_time now) {
    for (unsigned i = 0; i < SER8254_CHANNELS; ++i)
        counter_reset(&p->channel[i], now);
}

static void counter_reset(Ser8254Counter *c, ser_time now) {
    c->now = now;
    c->ltch_ctrl = false;
    c->ltch_cntr = false;
    c->read_order_low = true;   /* ByteOrder::LOW */
    c->write_order_low = true;
    c->control = 0x30;          /* Write BOTH / mode 0 / binary */
    c->active = false;
    c->counting = true;
    c->triggered = false;
    c->latched_counter = 0;
    c->latched_control = 0;
    c->write_latch = 0;
    c->gate = true;
}

static u8 counter_read(Ser8254Counter *c, ser_time now, bool consume) {
    if (c->ltch_ctrl) {
        if (consume) c->ltch_ctrl = false;
        return c->latched_control;
    }
    counter_advance(c, now);
    u16 read_data = c->ltch_cntr ? c->latched_counter : (u16)c->counter;
    switch (c->control & SER_CNTR_WRT_FRMT) {
        case SER_CNTR_WF_LATCH: break; /* unreachable */
        case SER_CNTR_WF_LOW:
            if (consume) c->ltch_cntr = false;
            return read_data & 0xFF;
        case SER_CNTR_WF_HIGH:
            if (consume) c->ltch_cntr = false;
            return (read_data >> 8) & 0xFF;
        case SER_CNTR_WF_BOTH:
            if (c->read_order_low) {
                if (consume) c->read_order_low = false;
                return read_data & 0xFF;
            } else {
                if (consume) { c->read_order_low = true; c->ltch_cntr = false; }
                return (read_data >> 8) & 0xFF;
            }
    }
    return 0xFF;
}

u8 ser8254_channel_read(Ser8254 *p, unsigned ch, ser_time now) {
    return counter_read(&p->channel[ch], now, true);
}
u8 ser8254_channel_peek(const Ser8254 *p, unsigned ch, ser_time now) {
    return counter_read((Ser8254Counter *)&p->channel[ch], now, false);
}

static void counter_write_load(Ser8254Counter *c, u16 value, ser_time now) {
    c->counter_load = value;
    u8 mode = c->control & SER_CNTR_MODE;
    if (mode == SER_CNTR_M0 || mode == SER_CNTR_M4)
        c->counter = value;
    if (!c->active && (mode == SER_CNTR_M3 || mode == SER_CNTR_M3_)) {
        if (serial_clock_is_periodic(&c->clock)) {
            c->counter = c->counter_load;
            int64_t half = (c->counter + 1) / 2; /* round up */
            ser_dur tick = serial_clock_get_total(&c->clock);
            ser_dur total = tick * (ser_dur)c->counter;
            ser_dur hi = tick * (ser_dur)half;
            serial_clock_set_periodic(&c->output, total, hi, now);
        }
    }
    if (!c->active && (mode == SER_CNTR_M2 || mode == SER_CNTR_M2_)) {
        if (serial_clock_is_periodic(&c->clock)) {
            c->counter = c->counter_load;
            ser_dur hi = serial_clock_get_total(&c->clock);
            ser_dur total = hi * (ser_dur)c->counter;
            serial_clock_set_periodic(&c->output, total, hi, now);
        }
    }
    if (mode == SER_CNTR_M0)
        serial_clock_set_state(&c->output, false, now);
    c->active = true; /* counter (re)armed after loading */
}

void ser8254_channel_write(Ser8254 *p, unsigned ch, u8 value, ser_time now) {
    Ser8254Counter *c = &p->channel[ch];
    counter_advance(c, now);
    switch (c->control & SER_CNTR_WRT_FRMT) {
        case SER_CNTR_WF_LATCH: break; /* unreachable */
        case SER_CNTR_WF_LOW:
            counter_write_load(c,
                (u16)((c->counter_load & 0xFF00) | value), now);
            break;
        case SER_CNTR_WF_HIGH:
            counter_write_load(c,
                (u16)((c->counter_load & 0x00FF) | ((u16)value << 8)), now);
            break;
        case SER_CNTR_WF_BOTH:
            if (c->write_order_low) {
                c->write_order_low = false;
                c->write_latch = value;
                if ((c->control & SER_CNTR_MODE) == SER_CNTR_M0)
                    c->counting = false; /* pause counting in mode 0 */
            } else {
                c->write_order_low = true;
                c->counting = true;
                counter_write_load(c,
                    (u16)(((u16)value << 8) | c->write_latch), now);
            }
            break;
    }
}

static void counter_write_control_word(Ser8254Counter *c, u8 value,
                                       ser_time now) {
    counter_advance(c, now);
    if ((value & SER_CNTR_WRT_FRMT) == 0) {
        counter_latch_counter(c, now);
        return;
    }
    c->control = value;
    c->write_order_low = true;
    c->counting = true;
    c->active = false;
    c->triggered = false;
    switch (c->control & SER_CNTR_MODE) {
        case SER_CNTR_M0:
            serial_clock_set_state(&c->output, false, now);
            break;
        default:
            serial_clock_set_state(&c->output, true, now);
            break;
    }
}

void ser8254_write_control(Ser8254 *p, u8 value, ser_time now) {
    if ((value & 0xC0) != 0xC0) {
        counter_write_control_word(&p->channel[(value >> 6) & 3],
                                   value & 0x3F, now);
    } else {
        /* Read-Back Command */
        if (value & 0x02) counter_latch_status(&p->channel[0], now);
        if (value & 0x04) counter_latch_status(&p->channel[1], now);
        if (value & 0x08) counter_latch_status(&p->channel[2], now);
        if (value & 0x10) { /* status selected; bits below are count */ }
        if (!(value & 0x10)) { /* latch count */
            if (value & 0x02) counter_latch_counter(&p->channel[0], now);
            if (value & 0x04) counter_latch_counter(&p->channel[1], now);
            if (value & 0x08) counter_latch_counter(&p->channel[2], now);
        }
    }
}

static void counter_latch_status(Ser8254Counter *c, ser_time now) {
    counter_advance(c, now);
    if (!c->ltch_ctrl) {
        c->ltch_ctrl = true;
        u8 out = serial_clock_get_state(&c->output, now) ? 0x80 : 0;
        c->latched_control = out | c->control;
    }
}

static void counter_latch_counter(Ser8254Counter *c, ser_time now) {
    counter_advance(c, now);
    if (!c->ltch_cntr) {
        c->ltch_cntr = true;
        c->read_order_low = true;
        c->latched_counter = (u16)c->counter;
    }
}

void ser8254_set_gate(Ser8254 *p, unsigned ch, bool status, ser_time now) {
    Ser8254Counter *c = &p->channel[ch];
    counter_advance(c, now);
    if (c->gate != status) {
        c->gate = status;
        switch (c->control & SER_CNTR_MODE) {
            case SER_CNTR_M0:
            case SER_CNTR_M4:
                break; /* gate is count-enable only */
            case SER_CNTR_M1:
                if (c->gate && c->active) {
                    c->counter = c->counter_load;
                    serial_clock_set_state(&c->output, false, now);
                    c->triggered = true;
                }
                break;
            case SER_CNTR_M2: case SER_CNTR_M2_:
            case SER_CNTR_M3: case SER_CNTR_M3_:
                if (c->gate) {
                    if (serial_clock_is_periodic(&c->clock)) {
                        c->counter = c->counter_load;
                        ser_dur hi = serial_clock_get_total(&c->clock);
                        ser_dur total = hi * (ser_dur)c->counter;
                        serial_clock_set_periodic(&c->output, total, hi, now);
                    }
                } else {
                    serial_clock_set_state(&c->output, true, now);
                }
                break;
            case SER_CNTR_M5:
                if (c->gate && c->active) {
                    c->counter = c->counter_load;
                    c->triggered = true;
                }
                break;
        }
    }
}

static void counter_advance(Ser8254Counter *c, ser_time now) {
    unsigned ticks = serial_clock_ticks_between(&c->clock, c->now, now);
    c->now = now;
    u8 mode = c->control & SER_CNTR_MODE;
    switch (mode) {
        case SER_CNTR_M0:
            if (c->gate && c->counting) {
                c->counter -= (int64_t)ticks;
                if (c->counter < 0) {
                    c->counter = (int64_t)(uint16_t)c->counter;
                    if (c->active) {
                        serial_clock_set_state(&c->output, false, now);
                        c->active = false;
                    }
                }
            }
            break;
        case SER_CNTR_M1:
            c->counter -= (int64_t)ticks;
            if (c->triggered && (c->counter < 0)) {
                serial_clock_set_state(&c->output, true, now);
                c->triggered = false;
            }
            c->counter = (int64_t)(uint16_t)c->counter;
            break;
        case SER_CNTR_M2: case SER_CNTR_M2_:
            if (c->gate) {
                c->counter -= (int64_t)ticks;
                if (c->active) {
                    if (c->counter_load != 0)
                        c->counter %= c->counter_load;
                    else
                        c->counter = 0;
                }
            }
            break;
        case SER_CNTR_M3: case SER_CNTR_M3_:
            if (c->gate) {
                c->counter -= 2 * (int64_t)ticks;
                if (c->active) {
                    if (c->counter_load != 0)
                        c->counter %= c->counter_load;
                    else
                        c->counter = 0;
                }
            }
            break;
        case SER_CNTR_M4:
            if (c->gate) {
                c->counter -= (int64_t)ticks;
                if (c->active) {
                    if (c->counter == 0)
                        serial_clock_set_state(&c->output, false, now);
                    else if (c->counter < 0) {
                        serial_clock_set_state(&c->output, true, now);
                        c->active = false;
                    }
                }
                c->counter = (int64_t)(uint16_t)c->counter;
            }
            break;
        case SER_CNTR_M5:
            c->counter -= (int64_t)ticks;
            if (c->triggered) {
                if (c->counter == 0)
                    serial_clock_set_state(&c->output, false, now);
                if (c->counter < 0) {
                    serial_clock_set_state(&c->output, true, now);
                    c->triggered = false;
                }
            }
            c->counter = (int64_t)(uint16_t)c->counter;
            break;
    }
}

Ser8254Counter *ser8254_counter(Ser8254 *p, unsigned ch) {
    return (ch < SER8254_CHANNELS) ? &p->channel[ch] : NULL;
}
const SerialClock *ser8254_output(const Ser8254 *p, unsigned ch) {
    return (ch < SER8254_CHANNELS) ? &p->channel[ch].output : NULL;
}