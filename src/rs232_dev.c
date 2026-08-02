/* rs232_dev.c - see rs232_dev.h. Combines the host transport (rs232.h), the
 * 8251 USART and 8254 timer into the MSX RS-232C interface behind ports 80h
 * to 87h, following the openMSX MSXRS232 port map:
 *
 *   80h  UART data           (8251)
 *   81h  UART status/cmd     (8251)
 *   82h  status sense (read) / IRQ mask (write)
 *   83h  unused              (0xFF)
 *   84h  timer counter 0     (8254)
 *   85h  timer counter 1
 *   86h  timer counter 2
 *   87h  timer command/control
 *
 * openMSX wires the 8254 counters 0 and 1 (1.8432MHz inputs) into the 8251
 * USART clock and counter 2's output into the status-sense bit 6; that wiring
 * is reproduced here via SerialClock listeners. openMSX's Scheduler is replaced
 * by rs232dev_io_advance(), driven from whole emulated Z80 cycles.
 */

#include "rs232_dev.h"

#include <stdlib.h>
#include <string.h>

#include "rs232.h"
#include "serial_8251.h"
#include "serial_8254.h"
#include "serial_timing.h"

struct Rs232Device {
    bool present;              /* config enabled */
    bool backend_active;       /* host backend actually running */
    ser_time now;              /* in SERIAL_MAIN_FREQ units */

    Rs232 backend;
    Ser8251 uart;
    Ser8254 pit;

    bool irq_mask;             /* port 82h write bit0 (no IRQ raised yet) */

    bool led_rx;
    bool led_tx;
};

/* ---- 8251 <-> host backend glue (Ser8251Interface) ---- */

#if RS232_HAVE_HOST_BACKEND
static void glue_recv_byte(u8 value, void *ctx) {
    Rs232Device *d = ctx;
    if (rs232_tx_push(&d->backend, value))
        d->led_tx = true;
}
static void glue_signal(void *ctx) {
    Rs232Device *d = ctx;
    u8 b;
    if (rs232_rx_pop(&d->backend, &b)) {
        d->led_rx = true;
        ser8251_recv_byte(&d->uart, b, d->now);
    }
}
#endif

static bool glue_get_cts(void *ctx) {
    Rs232Device *d = ctx;
    return d->backend_active;
}
static bool glue_get_dsr(void *ctx) {
    Rs232Device *d = ctx;
    return d->backend_active;
}

static void glue_set_ready(bool status, void *ctx) {
    Rs232Device *d = ctx;
    (void)status;
    (void)d;
}
static void glue_set_dtr(bool status, void *ctx) {
    (void)status;
    (void)ctx;
}
static void glue_set_rts(bool status, void *ctx) {
    (void)status;
    (void)ctx;
}
static void glue_set_data_bits(SerDataBits bits, void *ctx) {
    (void)bits;
    (void)ctx;
}
static void glue_set_stop_bits(SerStopBits bits, void *ctx) {
    (void)bits;
    (void)ctx;
}
static void glue_set_parity(bool enable, SerParity parity, void *ctx) {
    (void)enable;
    (void)parity;
    (void)ctx;
}

/* ---- 8254 counter 0/1 -> 8251 clock wiring ---- */

static void on_counter_output(void *ctx, SerialClock *pin, bool posedge) {
    Rs232Device *d = ctx;
    (void)posedge;
    if (serial_clock_is_periodic(pin)) {
        serial_clock_set_periodic(&d->uart.clock,
                                  serial_clock_get_total(pin),
                                  serial_clock_get_hi(pin), d->now);
    } else {
        serial_clock_set_state(&d->uart.clock,
                               serial_clock_get_state(pin, d->now), d->now);
    }
}

static void setup_pit(Ser8254 *pit, Rs232Device *d) {
    for (unsigned i = 0; i < SER8254_CHANNELS; ++i) {
        serial_clock_set_periodic(&pit->channel[i].clock,
                                  SERIAL_BAUD_TOTAL, SERIAL_BAUD_HI, 0);
    }
    /* Counters 0 and 1 generate the USART clock; counter 2 output feeds the
     * status-sense bit 6 (read via 82h). */
    serial_clock_set_listener(&pit->channel[0].output, d, on_counter_output);
    serial_clock_set_listener(&pit->channel[1].output, d, on_counter_output);
}

Rs232Device *rs232dev_create(void) {
    Rs232Device *d = calloc(1, sizeof(*d));
    if (!d) return NULL;

    Ser8251Interface io;
    memset(&io, 0, sizeof(io));
    io.ctx = d;
    io.set_ready = glue_set_ready;
    io.set_dtr = glue_set_dtr;
    io.set_rts = glue_set_rts;
    io.get_dsr = glue_get_dsr;
    io.get_cts = glue_get_cts;
    io.set_data_bits = glue_set_data_bits;
    io.set_stop_bits = glue_set_stop_bits;
    io.set_parity = glue_set_parity;
#if RS232_HAVE_HOST_BACKEND
    io.signal = glue_signal;
    io.recv_byte = glue_recv_byte;
#else
    io.signal = NULL;
    io.recv_byte = NULL;
#endif

    ser8251_init(&d->uart, &io);
    ser8254_init(&d->pit);
    setup_pit(&d->pit, d);
    d->now = 0;
    return d;
}

void rs232dev_destroy(Rs232Device *d) {
    if (!d) return;
#if RS232_HAVE_HOST_BACKEND
    rs232_shutdown(&d->backend);
#endif
    free(d);
}

bool rs232dev_set_enabled(Rs232Device *d, bool enabled) {
    if (!d) return false;
    d->present = enabled;
    d->backend_active = false;
#if RS232_HAVE_HOST_BACKEND
    if (enabled) {
        /* pty backend with a stable alias so picocom/minicom can attach. */
        rs232_init(&d->backend, true, "pty", 0, "/tmp/1983-rs232");
        d->backend_active = d->backend.present;
    } else {
        rs232_shutdown(&d->backend);
    }
#else
    (void)enabled;
#endif
    return d->present;
}

bool rs232dev_enabled(const Rs232Device *d) {
    return d && d->present;
}

static u8 status_sense(Rs232Device *d, ser_time now) {
    u8 result = 0xFF;
    if (d->backend_active) {
        result &= ~0x01;   /* CD active */
        result &= ~0x02;   /* RI active */
        result &= ~0x80;   /* CTS active */
    }
    /* Timer output from 8254 counter 2, when periodic/high. */
    if (serial_clock_get_state(&d->pit.channel[2].output, now))
        result &= ~0x40;
    if (d->irq_mask)
        result &= ~0x08;   /* IRQ-mask readback (Toshiba-style) */
    return result;
}

bool rs232dev_io_read(void *context, u16 port, u8 *value) {
    Rs232Device *d = context;
    u8 low;
    if (!d || !value)
        return false;
    low = (u8)port;
    if (low < 0x80 || low > 0x87)
        return false;
    if (!d->present)
        return false;

    switch (low) {
        case 0x80:
        case 0x81:
            *value = ser8251_read_io(&d->uart, low, d->now);
            break;
        case 0x82:
            *value = status_sense(d, d->now);
            break;
        case 0x83:
            *value = 0xFF;
            break;
        case 0x84:
        case 0x85:
        case 0x86:
            *value = ser8254_channel_read(&d->pit, low - 0x84, d->now);
            break;
        case 0x87:
            *value = 0xFF; /* control register is write-only */
            break;
    }
    return true;
}

bool rs232dev_io_write(void *context, u16 port, u8 value) {
    Rs232Device *d = context;
    u8 low;
    if (!d)
        return false;
    low = (u8)port;
    if (low < 0x80 || low > 0x87)
        return false;
    if (!d->present)
        return false;

    switch (low) {
        case 0x80:
        case 0x81:
            ser8251_write_io(&d->uart, low, value, d->now);
            break;
        case 0x82:
            d->irq_mask = (value & 1) == 0;
            break;
        case 0x83:
            break;
        case 0x84:
        case 0x85:
        case 0x86:
            ser8254_channel_write(&d->pit, low - 0x84, value, d->now);
            break;
        case 0x87:
            ser8254_write_control(&d->pit, value, d->now);
            break;
    }
    return true;
}

void rs232dev_io_reset(void *context) {
    Rs232Device *d = context;
    if (!d) return;
    ser8251_reset(&d->uart, d->now);
    ser8254_reset(&d->pit, d->now);
    d->irq_mask = false;
}

void rs232dev_io_advance(void *context, unsigned cycles) {
    Rs232Device *d = context;
    if (!d || !d->present)
        return;
    d->now += (ser_dur)cycles * SERIAL_Z80_CYCLE_TICKS;
#if RS232_HAVE_HOST_BACKEND
    rs232_poll(&d->backend);
    /* A byte that arrived after the RXE-enable pull is never re-pulled by a
     * deadline; deliver it now if the USART is waiting for a character. */
    if (ser8251_is_recv_enabled(&d->uart) &&
        ser8251_is_recv_ready(&d->uart)) {
        u8 b;
        if (rs232_rx_pop(&d->backend, &b)) {
            d->led_rx = true;
            ser8251_recv_byte(&d->uart, b, d->now);
        }
    }
#endif
    ser8251_advance(&d->uart, d->now);
}

bool rs232dev_take_rx_activity(Rs232Device *d) {
    if (!d) return false;
    bool a = d->led_rx;
    d->led_rx = false;
    return a;
}
bool rs232dev_take_tx_activity(Rs232Device *d) {
    if (!d) return false;
    bool a = d->led_tx;
    d->led_tx = false;
    return a;
}

const char *rs232dev_host_device(const Rs232Device *d) {
#if RS232_HAVE_HOST_BACKEND
    if (d && d->backend.present) {
        if (d->backend.pty_link[0]) return d->backend.pty_link;
        return d->backend.pty_slave;
    }
#else
    (void)d;
#endif
    return "";
}