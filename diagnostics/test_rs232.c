/* test_rs232.c - RS-232 host backend (PTY/TCP + ring buffers) self-test. */

#include "rs232.h"
#include "types.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
    else       { printf("ok:   %s\n", what); }
}

#ifndef _WIN32
static int read_slave(const char *slave) {
    /* Open the host-facing slave side once more and read whatever the
     * master has already received, so the loopback can observe TX. */
    int fd = open(slave, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -2;
    char c = '\0';
    ssize_t n = read(fd, &c, 1);
    close(fd);
    return n == 1 ? (unsigned char)c : -1;
}
#endif

int main(void) {
    Rs232 r;

    /* Ring-buffer FIFO behaviour (host-side only). */
    rs232_init(&r, true, "pty", 0, NULL);
    check(r.present, "pty backend initializes");
    check(rs232_tx_push(&r, 0x41) == true, "tx_push accepts");
    rs232_poll(&r); /* should not crash with no peer */
    rs232_shutdown(&r);
    check(!r.present, "shutdown clears present");

    /* Loopback: push into TX, poll moves TX->host, type into master's
     * slave, poll moves back into RX, then pop reads it. */
    rs232_init(&r, true, "pty", 0, NULL);
    check(r.pty_slave[0] != '\0', "pty slave node named");

    rs232_tx_push(&r, 'H');
    rs232_poll(&r);
    int back = read_slave(r.pty_slave);
    check(back == 'H', "TX byte surfaces on host pty slave");

    int sfd = open(r.pty_slave, O_RDWR | O_NOCTTY | O_NONBLOCK);
    check(sfd >= 0, "reopen slave for loopback input");
    if (sfd >= 0) {
        if (write(sfd, "Z", 1) == 1) {
            rs232_poll(&r);
            u8 got = 0;
            check(rs232_rx_pop(&r, &got) && got == 'Z', "guest reads host byte via RX");
        }
        close(sfd);
    }

    rs232_shutdown(&r);

    /* Non-PTY enable=false leaves port absent. */
    rs232_init(&r, false, "pty", 0, NULL);
    check(!r.present, "disabled cfg leaves RS232 absent");
    rs232_shutdown(&r);

    if (failures) { fprintf(stderr, "%d test(s) FAILED\n", failures); return 1; }
    printf("all rs232 backend tests passed\n");
    return 0;
}