/* test_rs232_dev.c - end-to-end RS-232C device test through the MSX I/O
 * window (80h-87h) and a real host PTY (Linux/BSD only). Verifies the
 * port map, the 8254->8251 clock chain, TX host delivery and RX capture.
 */

#include "rs232.h"
#include "rs232_dev.h"
#include "types.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;
static void check(int cond, const char *what) {
    if (!cond) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
    else       { printf("ok:   %s\n", what); }
}

#if RS232_HAVE_HOST_BACKEND
#include <fcntl.h>
#include <unistd.h>

static int slave_read_byte(const char *slave) {
    int fd = open(slave, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -2;
    char c = 0;
    ssize_t n = read(fd, &c, 1);
    close(fd);
    return n == 1 ? (unsigned char)c : -1;
}
#endif

int main(void) {
    Rs232Device *dev;
    u16 count;
    u8 v;

#if !RS232_HAVE_HOST_BACKEND
    printf("SKIP: serial host backend unavailable on this platform\n");
    return 77;
#else
    dev = rs232dev_create();
    check(dev != NULL, "rs232 device creates");
    check(rs232dev_set_enabled(dev, true), "rs232 device enables");

    /* Exercise the creation lifecycle directly, without an artificial machine
     * reset after the device is attached. Counter gates are hardware-high and
     * a freshly programmed application counter must start immediately. */
    rs232dev_io_write(dev, 0x87, 0xB0);          /* ch2: mode0, both */
    rs232dev_io_write(dev, 0x86, 0xFF);
    rs232dev_io_write(dev, 0x86, 0xFF);
    rs232dev_io_advance(dev, 1000);
    rs232dev_io_write(dev, 0x87, 0x80);          /* latch ch2 */
    rs232dev_io_read(dev, 0x86, &v);
    count = v;
    rs232dev_io_read(dev, 0x86, &v);
    count |= (u16)v << 8;
    check(count < 0xFFFF, "8254 counter advances immediately after creation");

    /* Program 8254 counter 0 (mode 3, both bytes) so its output feeds the
     * USART clock. */
    rs232dev_io_write(dev, 0x87, 0x36);          /* ch0: mode3, both */
    rs232dev_io_write(dev, 0x84, 0x00);          /* load low  */
    rs232dev_io_write(dev, 0x84, 0x02);          /* load high -> 0x0200 */

    /* Program the 8251: 8N1 asynch, then TXEN|RXE|DTR|RTS. */
    rs232dev_io_write(dev, 0x81, 0x4E);
    rs232dev_io_write(dev, 0x81, 0x37);

    const char *devnode = rs232dev_host_device(dev);
    check(devnode[0] != '\0', "rs232 host device name available");

    /* TX: guest writes 'A' to port 80h, time passes, byte reaches host pty.
     * One advance runs the USART (pushing into the TX ring); a follow-up
     * advance drains that ring to the host fd. */
    rs232dev_io_write(dev, 0x80, 'A');
    rs232dev_io_advance(dev, 200000);   /* ~0.5s of Z80 cycles */
    rs232dev_io_advance(dev, 16);
    int got = slave_read_byte(devnode);
    check(got == 'A', "TX byte surfaced on host pty");

    /* RX: host writes 'Z' into the pty; guest reads it back on port 80h. */
    int sfd = open(devnode, O_RDWR | O_NOCTTY | O_NONBLOCK);
    check(sfd >= 0, "open pty slave for RX injection");
    if (sfd >= 0) {
        if (write(sfd, "Z", 1) == 1) {
            rs232dev_io_advance(dev, 200000);
            v = 0;
            rs232dev_io_read(dev, 0x80, &v);
            check(v == 'Z', "RX byte read back through port 80h");
        }
        close(sfd);
    }

    /* Status-sense port 82h is readable. */
    check(rs232dev_io_read(dev, 0x82, &v), "port 82h status readable");

    /* Toggle the interface off cleanly. */
    rs232dev_set_enabled(dev, false);
    check(!rs232dev_enabled(dev), "rs232 device disables");
    rs232dev_destroy(dev);

    if (failures) { fprintf(stderr, "%d test(s) FAILED\n", failures); return 1; }
    printf("all rs232 device tests passed\n");
    return 0;
#endif
}
