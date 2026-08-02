/* serial_8251.c - Intel 8251 USART, a C port of openMSX src/serial/I8251.cc
 * (GPL-2.0+). The openMSX Scheduler sync points are reduced to pending rx/tx
 * deadlines fired by ser8251_advance(); every register-visible transition
 * mirrors the original.
 */

#include "serial_8251.h"

#include <stdbool.h>
#include <string.h>

/* Status register bits. */
#define STAT_TXRDY   0x01
#define STAT_RXRDY   0x02
#define STAT_TXEMPTY 0x04
#define STAT_PE      0x08
#define STAT_OE      0x10
#define STAT_FE      0x20
#define STAT_SYN_BRK 0x40
#define STAT_DSR     0x80

/* Mode register field masks (8251 datasheet / openMSX). */
#define MODE_BAUDRATE    0x03
#define MODE_SYNCHRONOUS 0x00
#define MODE_RATE1       0x01
#define MODE_RATE16      0x02
#define MODE_RATE64      0x03
#define MODE_WORD_LEN    0x0C
#define MODE_5BIT        0x00
#define MODE_6BIT        0x04
#define MODE_7BIT        0x08
#define MODE_8BIT        0x0C
#define MODE_PARITY_EVEN 0x10
#define MODE_PARITEVEN   0x20
#define MODE_STOP_BITS   0xC0
#define MODE_STOP_INV    0x00
#define MODE_STOP_1      0x40
#define MODE_STOP_15     0x80
#define MODE_STOP_2      0xC0
#define MODE_SINGLE_SYNC 0x80

/* Command register bits. */
#define CMD_TXEN    0x01
#define CMD_DTR     0x02
#define CMD_RXE     0x04
#define CMD_SBRK    0x08
#define CMD_RST_ERR 0x10
#define CMD_RTS     0x20
#define CMD_RESET   0x40
#define CMD_HUNT    0x80

static unsigned divisor_of(u8 rate) {
    switch (rate) {
        case MODE_SYNCHRONOUS: return 1;
        case MODE_RATE1:       return 1;
        case MODE_RATE16:      return 16;
        case MODE_RATE64:      return 64;
        default:               return 1;
    }
}

static void uart_send(Ser8251 *s, u8 value, ser_time t);
static void ser8251_write_command(Ser8251 *s, u8 value, ser_time t);

void ser8251_set_data_bits(Ser8251 *s, SerDataBits bits) {
    s->recv_data_bits = bits;
}
void ser8251_set_stop_bits(Ser8251 *s, SerStopBits bits) {
    s->recv_stop_bits = bits;
}
void ser8251_set_parity(Ser8251 *s, bool enable, SerParity parity) {
    s->recv_parity_enabled = enable;
    s->recv_parity = parity;
}

void ser8251_init(Ser8251 *s, const Ser8251Interface *io) {
    memset(s, 0, sizeof(*s));
    if (io) s->io = *io;
    serial_clock_init(&s->clock);
    ser8251_reset(s, 0);
}

void ser8251_reset(Ser8251 *s, ser_time t) {
    (void)t;
    s->char_length = 0;
    s->recv_data_bits = SER_DATA_8;
    s->recv_stop_bits = SER_STOP_1;
    s->recv_parity = SER_PARITY_EVEN;
    s->recv_parity_enabled = false;
    s->recv_buf = 0;
    s->recv_ready = false;
    s->send_byte = 0;
    s->send_buffer = 0;
    s->mode = 0;
    s->sync1 = s->sync2 = 0;

    s->status = STAT_TXRDY | STAT_TXEMPTY;
    s->command = 0xFF; /* force all bits to change */
    ser8251_write_command(s, 0, t);
    s->cmd_phase = SER_CMD_MODE;

    s->recv_pending = false;
    s->trans_pending = false;
}

static void set_mode(Ser8251 *s, u8 new_mode) {
    s->mode = new_mode;

    SerDataBits db = SER_DATA_8;
    switch (s->mode & MODE_WORD_LEN) {
        case MODE_5BIT: db = SER_DATA_5; break;
        case MODE_6BIT: db = SER_DATA_6; break;
        case MODE_7BIT: db = SER_DATA_7; break;
        default:        db = SER_DATA_8; break;
    }
    s->recv_data_bits = db;
    if (s->io.set_data_bits) s->io.set_data_bits(db, s->io.ctx);

    SerStopBits sb = SER_STOP_1;
    switch (s->mode & MODE_STOP_BITS) {
        case MODE_STOP_INV: sb = SER_STOP_INV; break;
        case MODE_STOP_1:   sb = SER_STOP_1;   break;
        case MODE_STOP_15:  sb = SER_STOP_1_5; break;
        default:            sb = SER_STOP_2;   break;
    }
    s->recv_stop_bits = sb;
    if (s->io.set_stop_bits) s->io.set_stop_bits(sb, s->io.ctx);

    bool parity_enable = (s->mode & MODE_PARITY_EVEN) != 0;
    SerParity parity = (s->mode & MODE_PARITEVEN)
        ? SER_PARITY_EVEN : SER_PARITY_ODD;
    s->recv_parity_enabled = parity_enable;
    s->recv_parity = parity;
    if (s->io.set_parity) s->io.set_parity(parity_enable, parity, s->io.ctx);

    unsigned baudrate = divisor_of(s->mode & MODE_BAUDRATE);
    unsigned data_bits = (unsigned)db;    /* 5..8 */
    unsigned stop_stmt = (unsigned)sb;    /* 0|1|2|10 */

    /* open: ((2*(1+bits+parity) + stop) * baud) / 2 */
    s->char_length = (unsigned)
        ((((2 * (1 + data_bits + (parity_enable ? 1 : 0))) + stop_stmt)
          * baudrate) / 2);
}

static void ser8251_write_command(Ser8251 *s, u8 value, ser_time t) {
    (void)t;
    u8 old_command = s->command;
    s->command = value;

    if (s->io.set_rts) s->io.set_rts((s->command & CMD_RTS) != 0, s->io.ctx);
    if (s->io.set_dtr) s->io.set_dtr((s->command & CMD_DTR) != 0, s->io.ctx);

    if (!(s->command & CMD_TXEN)) {
        s->trans_pending = false;
        s->status |= STAT_TXRDY | STAT_TXEMPTY;
    }
    if (s->command & CMD_RST_ERR)
        s->status &= ~(STAT_PE | STAT_OE | STAT_FE);
    if (s->command & CMD_SBRK) {
        /* TODO */
    }
    if (s->command & CMD_HUNT) {
        /* TODO */
    }

    if ((s->command ^ old_command) & CMD_RXE) {
        if (s->command & CMD_RXE) {
            s->status &= ~(STAT_PE | STAT_OE | STAT_FE);
            s->recv_ready = true;
        } else {
            s->recv_pending = false;
            s->status &= ~(STAT_PE | STAT_OE | STAT_FE);
            s->status &= ~STAT_RXRDY;
        }
        if (s->io.signal) s->io.signal(s->io.ctx);
    }
}

static u8 read_trans(Ser8251 *s, ser_time t) {
    (void)t;
    s->status &= ~STAT_RXRDY;
    if (s->io.set_ready) s->io.set_ready(false, s->io.ctx);
    return s->recv_buf;
}

static u8 read_status(Ser8251 *s) {
    u8 result = s->status;
    if (s->io.get_dsr && s->io.get_dsr(s->io.ctx))
        result |= STAT_DSR;
    return result;
}

u8 ser8251_read_io(Ser8251 *s, u16 port, ser_time t) {
    switch (port & 1) {
        case 0: return read_trans(s, t);
        case 1: return read_status(s);
        default: return 0xff;
    }
}

u8 ser8251_peek_io(const Ser8251 *s, u16 port) {
    switch (port & 1) {
        case 0: return s->recv_buf;
        case 1: return s->status;
        default: return 0xff;
    }
}

/* Start a transmit of `value`; schedule its completion one character time
 * later (port of openMSX I8251::send()).
 */
static void uart_send(Ser8251 *s, u8 value, ser_time t) {
    s->status &= ~STAT_TXEMPTY;
    s->send_byte = value;
    if (serial_clock_is_periodic(&s->clock)) {
        ser_time next = t +
            (ser_dur)serial_clock_get_total(&s->clock) * s->char_length;
        s->trans_deadline = next;
        s->trans_pending = true;
    }
}

void ser8251_write_io(Ser8251 *s, u16 port, u8 value, ser_time t) {
    if ((port & 1) == 0) {
        /* data register (write trans) */
        if (!(s->command & CMD_TXEN))
            return;
        if (s->status & STAT_TXEMPTY)
            uart_send(s, value, t);
        else {
            s->send_buffer = value;
            s->status &= ~STAT_TXRDY;
        }
        return;
    }
    /* control/command register */
    switch (s->cmd_phase) {
        case SER_CMD_MODE:
            set_mode(s, value);
            if ((s->mode & MODE_BAUDRATE) == MODE_SYNCHRONOUS)
                s->cmd_phase = SER_CMD_SYNC1;
            else
                s->cmd_phase = SER_CMD_CMD;
            break;
        case SER_CMD_SYNC1:
            s->sync1 = value;
            s->cmd_phase = (s->mode & MODE_SINGLE_SYNC)
                ? SER_CMD_CMD : SER_CMD_SYNC2;
            break;
        case SER_CMD_SYNC2:
            s->sync2 = value;
            s->cmd_phase = SER_CMD_CMD;
            break;
        case SER_CMD_CMD:
            if (value & CMD_RESET)
                s->cmd_phase = SER_CMD_MODE;
            else
                ser8251_write_command(s, value, t);
            break;
    }
}

void ser8251_recv_byte(Ser8251 *s, u8 value, ser_time t) {
    if (!s->recv_ready || !(s->command & CMD_RXE))
        return;
    if (s->status & STAT_RXRDY) {
        s->status |= STAT_OE;
    } else {
        s->recv_buf = value;
        s->status |= STAT_RXRDY;
        if (s->io.set_ready) s->io.set_ready(true, s->io.ctx);
    }
    s->recv_ready = false;
    if (serial_clock_is_periodic(&s->clock)) {
        ser_time next = t +
            (ser_dur)serial_clock_get_total(&s->clock) * s->char_length;
        s->recv_deadline = next;
        s->recv_pending = true;
    }
}

bool ser8251_is_recv_enabled(const Ser8251 *s) {
    return (s->command & CMD_RXE) != 0;
}
bool ser8251_is_recv_ready(const Ser8251 *s) {
    return s->recv_ready;
}

/* Fire pending rx/tx sync points whose host-clock time arrived. openMSX
 * fired these from its Scheduler at the exact EmuTime; 1983 drives the serial
 * clock from whole emulated Z80 cycles, so we fire the freshly-reached point
 * (each may chain further work) whenever the clock is advanced. */
void ser8251_advance(Ser8251 *s, ser_time now) {
    if (s->trans_pending && now >= s->trans_deadline) {
        s->trans_pending = false;
        if (!(s->status & STAT_TXEMPTY) && (s->command & CMD_TXEN)) {
            /* execTrans */
            if (s->io.recv_byte)
                s->io.recv_byte(s->send_byte, s->io.ctx);
            if (s->status & STAT_TXRDY) {
                s->status |= STAT_TXEMPTY;
            } else {
                s->status |= STAT_TXRDY;
                uart_send(s, s->send_buffer, now);
            }
        }
    }
    if (s->recv_pending && now >= s->recv_deadline) {
        s->recv_pending = false;
        if (s->command & CMD_RXE) {
            s->recv_ready = true;
            if (s->io.signal) s->io.signal(s->io.ctx);
        }
    }
}