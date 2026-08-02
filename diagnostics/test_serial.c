/* test_serial.c - register-level self-tests for the openMSX-derived 8251
 * USART and 8254 timer chips (portable: no host pty/socket required).
 */

#include "serial_8251.h"
#include "serial_8254.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

static int failures;
static void check(int cond, const char *what) {
    if (!cond) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
    else       { printf("ok:   %s\n", what); }
}

/* ---- 8251 RX capture hook ---- */
static u8 rx_byte = 0;
static int rx_n = 0;
static void on_recv_byte(u8 v, void *ctx) { (void)ctx; rx_byte = v; rx_n++; }

static Ser8251Interface make_iface(void) {
    Ser8251Interface io;
    memset(&io, 0, sizeof(io));
    io.recv_byte = on_recv_byte;
    return io;
}

static void test_8251(void) {
    Ser8251Interface io = make_iface();
    Ser8251 s;
    ser8251_init(&s, &io);
    /* Real MSX plumbing feeds the 8251 from a periodic 1.8432MHz pin. */
    serial_clock_set_periodic(&s.clock, SERIAL_BAUD_TOTAL, SERIAL_BAUD_HI, 0);

    check((s.status & (0x01 | 0x04)) == (0x01 | 0x04),
          "8251 reset -> TXRDY+TXEMPTY");

    ser_time t = 1000;
    /* 8 data bits, no parity, 1 stop, baud /1 (asynch). */
    ser8251_write_io(&s, 0x81, 0x4E, t);
    check(s.cmd_phase == SER_CMD_CMD, "8251 asynch mode -> command phase");
    ser8251_write_io(&s, 0x81, 0x37, t); /* TXEN|RXE|DTR|RTS */
    check((s.command & 0x05) == 0x05, "8251 TXEN+RXE accepted");

    /* Transmit: TXEMPTY clears, then after one char time the byte emerges
     * through the recv_byte sink and TXEMPTY returns. */
    ser8251_write_io(&s, 0x80, 'A', t);
    check((s.status & 0x04) == 0, "8251 TXEMPTY clears on write");
    ser_time done = t + (ser_dur)SERIAL_BAUD_TOTAL * (s.char_length ? s.char_length : 1) * 4;
    ser8251_advance(&s, done);
    check(rx_n == 1 && rx_byte == 'A', "8251 TX byte reaches host sink");
    check((s.status & (0x01 | 0x04)) == (0x01 | 0x04),
          "8251 TX again idle after char");

    /* Receive: host pushes a byte -> RXRDY set, host read returns it. */
    rx_n = 0;
    ser8251_recv_byte(&s, 'Z', done);
    check((s.status & 0x02) != 0, "8251 RXRDY set by host byte");
    check(ser8251_read_io(&s, 0x80, done) == 'Z', "8251 data read returns host byte");
    check((s.status & 0x02) == 0, "8251 RXRDY clears on data read");
}

static void test_8254(void) {
    Ser8254 p;
    ser8254_init(&p);
    ser8254_reset(&p, 0);

    ser_time tick = SERIAL_BAUD_TOTAL; /* one 1.8432MHz period */

    /* Make counter 0's input clock periodic so the time base counts. */
    serial_clock_set_periodic(&p.channel[0].clock, SERIAL_BAUD_TOTAL,
                              SERIAL_BAUD_HI, 0);

    ser_time t = 0;
    /* Program counter 0: mode 0, write BOTH bytes, binary. */
    ser8254_write_control(&p, 0x30, t);   /* control word: ch0, mode0, both */
    ser8254_channel_write(&p, 0, 0x10, t);      /* low byte */
    t += tick * 1000;
    ser8254_channel_write(&p, 0, 0x30, t);      /* high byte -> 0x3010 */

    /* Advance another 500 clock periods and read back. */
    ser_time t1 = t + tick * 500;
    u8 l = ser8254_channel_read(&p, 0, t1);
    u8 h = ser8254_channel_read(&p, 0, t1);
    unsigned c = (unsigned)((h << 8) | l);
    check(c == 0x3010 - 500, "8254 mode0 countdown tracks input clock");
}

int main(void) {
    test_8251();
    test_8254();
    if (failures) { fprintf(stderr, "%d test(s) FAILED\n", failures); return 1; }
    printf("all serial chip tests passed\n");
    return 0;
}