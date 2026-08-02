/* serial_8251.h - Intel 8251 USART, a C port of openMSX src/serial/I8251.hh.
 * GPL-2.0+ openMSX code reused in 1983 (GPL-2.0-only); algorithms, constants
 * and comments are retained from the original. Timing is supplied by
 * serial_timing.h; the openMSX Scheduler sync points are reduced to pending
 * rx/tx deadlines that the host fires from serial_8251_advance().
 */
#pragma once

#include "serial_timing.h"
#include "types.h"

#include <stdbool.h>

/* Serial framing parameters (mirror openMSX SerialDataInterface). */
typedef enum {
    SER_DATA_5 = 5,
    SER_DATA_6 = 6,
    SER_DATA_7 = 7,
    SER_DATA_8 = 8
} SerDataBits;

typedef enum {
    SER_STOP_INV = 0, /* undefined in mode */
    SER_STOP_1 = 1,
    SER_STOP_1_5 = 10,
    SER_STOP_2 = 2
} SerStopBits;

typedef enum { SER_PARITY_EVEN, SER_PARITY_ODD } SerParity;

/* MSX RS-232C glue points (port of openMSX I8251Interface). */
typedef struct Ser8251Interface {
    void *ctx;
    void (*set_ready)(bool status, void *ctx);
    void (*set_dtr)(bool status, void *ctx);
    void (*set_rts)(bool status, void *ctx);
    bool (*get_dsr)(void *ctx);
    bool (*get_cts)(void *ctx);
    void (*signal)(void *ctx);          /* backpressure: next input byte */

    /* Data/parity routed to the host termios (SerialDataInterface). */
    void (*set_data_bits)(SerDataBits bits, void *ctx);
    void (*set_stop_bits)(SerStopBits bits, void *ctx);
    void (*set_parity)(bool enable, SerParity parity, void *ctx);

    /* Output stream: a fully framed byte, one bit period at char * 8254. */
    void (*recv_byte)(u8 value, void *ctx);
} Ser8251Interface;

typedef enum {
    SER_CMD_MODE = 0,
    SER_CMD_SYNC1 = 1,
    SER_CMD_SYNC2 = 2,
    SER_CMD_CMD = 3
} SerCmdPhase;

typedef struct Ser8251 {
    Ser8251Interface io;      /* external RS-232 host half */
    SerialClock clock;        /* fed by 8254 counter 0/1 (1.8432MHz) */

    unsigned char_length;

    SerCmdPhase cmd_phase;
    SerDataBits recv_data_bits;
    SerStopBits recv_stop_bits;
    SerParity recv_parity;
    bool recv_parity_enabled;
    u8 recv_buf;
    bool recv_ready;

    u8 send_byte;
    u8 send_buffer;

    u8 status;
    u8 command;
    u8 mode;
    u8 sync1;
    u8 sync2;

    /* sync scheduling, replaced openMSX Schedulable (a time == -1 is idle).
     * ser_time is unsigned: keep a separate "pending" flag. */
    bool recv_pending;
    ser_time recv_deadline;
    bool trans_pending;
    ser_time trans_deadline;
} Ser8251;

void ser8251_init(Ser8251 *s, const Ser8251Interface *io);
void ser8251_reset(Ser8251 *s, ser_time t);

u8 ser8251_read_io(Ser8251 *s, u16 port, ser_time t);
u8 ser8251_peek_io(const Ser8251 *s, u16 port);
void ser8251_write_io(Ser8251 *s, u16 port, u8 value, ser_time t);

/* Called from the emulator clock with the serial device's idea of "now". */
void ser8251_advance(Ser8251 *s, ser_time now);

bool ser8251_is_recv_enabled(const Ser8251 *s);
bool ser8251_is_recv_ready(const Ser8251 *s);
void ser8251_recv_byte(Ser8251 *s, u8 value, ser_time t);

/* Host data-bits plumbing (SerialDataInterface). */
void ser8251_set_data_bits(Ser8251 *s, SerDataBits bits);
void ser8251_set_stop_bits(Ser8251 *s, SerStopBits bits);
void ser8251_set_parity(Ser8251 *s, bool enable, SerParity parity);