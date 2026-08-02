/* rs232 - host-side RS-232C backend (PTY or TCP) for 1983.
 *
 * This is the guest-agnostic serial transport, ported from the sibling
 * emulators: 1984's usifac.c and 1985's serial.c (which model the 8251
 * USART). Each "port" terminates either in a host pseudo-terminal or in a
 * TCP listener on localhost, with power-of-two ring buffers between the
 * host fd and the guest-facing byte API.
 *
 * The MSX I/O surface (the 8251 at ports 80h-87h) is not wired yet; this
 * header exposes only the backend lifecycle and an RX/TX byte pair for
 * whatever model is connected later. POSIX hosts (Linux, macOS, BSD) are
 * supported; Windows is disabled until the backend is ported.
 */
#pragma once

#include "types.h"
#include <stdbool.h>
#include <stddef.h>


#define RS232_RING 4096   /* power of two */

typedef enum {
    RS232_BACKEND_PTY = 0,
    RS232_BACKEND_TCP = 1,
} Rs232Backend;

typedef struct Rs232 {
    bool present;             /* mirror of cfg->rs232 at init time */
    Rs232Backend backend;

    /* PTY backend */
    int  pty_master;          /* -1 when closed */
    char pty_slave[64];       /* /dev/pts/N for the overlay */
    char pty_link[64];        /* stable host-side alias, e.g. /tmp/1983-rs232 */

    /* TCP backend */
    int  tcp_listen;          /* -1 when closed */
    int  tcp_client;          /* -1 when no client */
    int  tcp_port;

    /* Ring buffers - power-of-two sized, masked indices */
    u8     rx_buf[RS232_RING];
    size_t rx_head, rx_tail;  /* head=push (backend), tail=pop (guest) */
    u8     tx_buf[RS232_RING];
    size_t tx_head, tx_tail;  /* head=push (guest),   tail=pop (backend) */
} Rs232;

/* `backend` is "pty" or "tcp"; `tcp_port` is ignored for PTY.
 * `pty_link_path` is the stable host-side symlink to create for PTY
 * backends (e.g. "/tmp/1983-rs232"); NULL or empty disables the link.
 * Call rs232_init with enable=false to leave the port absent. */
void rs232_init(Rs232 *r, bool enable, const char *backend,
                int tcp_port, const char *pty_link_path);
void rs232_shutdown(Rs232 *r);

/* Drain backend -> RX, push TX -> backend. Call once per frame/audio tick. */
void rs232_poll(Rs232 *r);

/* Byte-level FIFO used by the USART model. */
bool rs232_rx_pop (Rs232 *r, u8 *out);
bool rs232_tx_push(Rs232 *r, u8 b);
bool rs232_rx_has (const Rs232 *r);