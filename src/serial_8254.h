/* serial_8254.h - Intel 8253/8254 3-channel counter/timer, a C port of
 * openMSX src/serial/I8254.hh (GPL-2.0+). openMSX's EmuTime scheduler input
 * is supplied by 1983's serial_timing.h; only latch/count semantics change at
 * the register level.
 */
#pragma once

#include "serial_timing.h"
#include "types.h"

#include <stdbool.h>
#include <stdint.h>

#define SER8254_CHANNELS 3

/* Control-word field bits (8254 datasheet / openMSX Counter). */
#define SER_CNTR_WRT_FRMT 0x30
#define SER_CNTR_WF_LATCH 0x00
#define SER_CNTR_WF_LOW   0x10
#define SER_CNTR_WF_HIGH  0x20
#define SER_CNTR_WF_BOTH  0x30
#define SER_CNTR_MODE     0x0E
#define SER_CNTR_M0       0x00
#define SER_CNTR_M1       0x02
#define SER_CNTR_M2       0x04
#define SER_CNTR_M3       0x06
#define SER_CNTR_M4       0x08
#define SER_CNTR_M5       0x0A
#define SER_CNTR_M2_      0x0C
#define SER_CNTR_M3_      0x0E

typedef struct Ser8254Counter {
    SerialClock clock;      /* input clock (1.8432MHz periodic)   */
    SerialClock output;     /* output pin; may carry a listener   */

    ser_time now;
    int64_t counter;
    u16 latched_counter;
    u16 counter_load;
    u8 control;
    u8 latched_control;
    bool ltch_ctrl;
    bool ltch_cntr;
    bool read_order_low;    /* ByteOrder::LOW */
    bool write_order_low;   /* ByteOrder::LOW */
    bool gate;
    bool active;
    bool triggered;
    bool counting;
    u8 write_latch;
} Ser8254Counter;

typedef struct Ser8254 {
    Ser8254Counter channel[SER8254_CHANNELS];
} Ser8254;

void ser8254_init(Ser8254 *p);

void ser8254_reset(Ser8254 *p, ser_time now);

/* Per-channel control-plane API (ports 4..6 = data, port 7 = control). */
u8 ser8254_channel_read(Ser8254 *p, unsigned ch, ser_time now);
u8 ser8254_channel_peek(const Ser8254 *p, unsigned ch, ser_time now);
void ser8254_channel_write(Ser8254 *p, unsigned ch, u8 value, ser_time now);
void ser8254_write_control(Ser8254 *p, u8 value, ser_time now);
void ser8254_set_gate(Ser8254 *p, unsigned ch, bool status, ser_time now);

Ser8254Counter *ser8254_counter(Ser8254 *p, unsigned ch);
const SerialClock *ser8254_output(const Ser8254 *p, unsigned ch);