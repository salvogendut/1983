/* rs232.c - see rs232.h. Backend ported from 1985's serial.c. */

/* _XOPEN_SOURCE hides Darwin's BSD interfaces unless this is requested. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#define _XOPEN_SOURCE 600   /* posix_openpt, grantpt, unlockpt, ptsname */
#define _DEFAULT_SOURCE     /* glibc: cfmakeraw, MSG_NOSIGNAL */
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define __BSD_VISIBLE 1     /* BSD: cfmakeraw, INADDR_LOOPBACK */
#endif

#include "rs232.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#if RS232_HAVE_HOST_BACKEND
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif

/* MSG_NOSIGNAL is a hidden/extension on some platforms; suppress SIGPIPE
 * differently in rs232_init(). Fallback must come after <sys/socket.h>. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MASK (RS232_RING - 1)

static inline size_t rsb_count(size_t head, size_t tail) {
    return (head - tail) & MASK;
}
static inline size_t rsb_space(size_t head, size_t tail) {
    return MASK - rsb_count(head, tail);
}
static inline bool rsb_empty(size_t head, size_t tail) { return head == tail; }

#if RS232_HAVE_HOST_BACKEND
static void rx_push(Rs232 *r, u8 b) {
    if (rsb_space(r->rx_head, r->rx_tail) == 0) return;
    r->rx_buf[r->rx_head & MASK] = b;
    r->rx_head = (r->rx_head + 1) & MASK;
}
bool rs232_rx_pop(Rs232 *r, u8 *out) {
    if (rsb_empty(r->rx_head, r->rx_tail)) return false;
    *out = r->rx_buf[r->rx_tail & MASK];
    r->rx_tail = (r->rx_tail + 1) & MASK;
    return true;
}
bool rs232_tx_push(Rs232 *r, u8 b) {
    if (rsb_space(r->tx_head, r->tx_tail) == 0) return false;
    r->tx_buf[r->tx_head & MASK] = b;
    r->tx_head = (r->tx_head + 1) & MASK;
    return true;
}
static bool tx_pop(Rs232 *r, u8 *out) {
    if (rsb_empty(r->tx_head, r->tx_tail)) return false;
    *out = r->tx_buf[r->tx_tail & MASK];
    r->tx_tail = (r->tx_tail + 1) & MASK;
    return true;
}
#endif /* host backend */
bool rs232_rx_has(const Rs232 *r) { return !rsb_empty(r->rx_head, r->rx_tail); }

#if RS232_HAVE_HOST_BACKEND
static int open_pty(Rs232 *r, const char *link_path) {
    int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("rs232: posix_openpt"); return -1; }
    if (grantpt(fd) < 0 || unlockpt(fd) < 0) {
        perror("rs232: grantpt/unlockpt"); close(fd); return -1;
    }
    const char *name = ptsname(fd);
    if (!name) { close(fd); return -1; }
    snprintf(r->pty_slave, sizeof(r->pty_slave), "%s", name);

    /* Open the slave once and put it in raw mode so the line discipline
     * survives later host-side opens; also keeps the /dev/pts/N node around
     * between user reconnects. See 1985/src/serial.c open_pty() rationale. */
    int sfd = open(name, O_RDWR | O_NOCTTY);
    if (sfd >= 0) {
        struct termios tio;
        if (tcgetattr(sfd, &tio) == 0) {
            cfmakeraw(&tio);
            tio.c_cflag |= CS8 | CREAD | CLOCAL;
            cfsetispeed(&tio, B9600);
            cfsetospeed(&tio, B9600);
            tcsetattr(sfd, TCSANOW, &tio);
        }
        close(sfd);
    }

    r->pty_master = fd;

    /* Stable host-side alias so the user doesn't have to chase the
     * randomised /dev/ptts/N on each launch. Replaces any prior link; NULL
     * or empty disables the symlink. */
    if (link_path && link_path[0]) {
        unlink(link_path);
        if (symlink(r->pty_slave, link_path) == 0)
            snprintf(r->pty_link, sizeof(r->pty_link), "%s", link_path);
        else
            r->pty_link[0] = '\0';
    } else {
        r->pty_link[0] = '\0';
    }

    if (r->pty_link[0])
        fprintf(stderr, "rs232: PTY ready at %s (alias %s)\n",
                 r->pty_slave, r->pty_link);
    else
        fprintf(stderr, "rs232: PTY ready at %s\n", r->pty_slave);
    return 0;
}

static int open_tcp(Rs232 *r, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("rs232: socket"); return -1; }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "rs232: bind localhost:%d failed: %s\n", port, strerror(errno));
        close(fd); return -1;
    }
    if (listen(fd, 1) < 0) { perror("rs232: listen"); close(fd); return -1; }

    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | (int)O_NONBLOCK);

    r->tcp_listen = fd;
    r->tcp_port   = port;
    fprintf(stderr, "rs232: TCP listening on localhost:%d\n", port);
    return 0;
}

static void close_fd(int *pfd) {
    if (*pfd >= 0) { close(*pfd); *pfd = -1; }
}
#endif /* host backend */

void rs232_init(Rs232 *r, bool enable, const char *backend, int tcp_port,
                const char *pty_link_path) {
    memset(r, 0, sizeof(*r));
    r->pty_master = -1;
    r->tcp_listen = -1;
    r->tcp_client = -1;
    r->present    = enable;
    if (!r->present) return;

#if !RS232_HAVE_HOST_BACKEND
    (void)backend; (void)tcp_port; (void)pty_link_path;
    fprintf(stderr, "rs232: backend not supported on Windows yet - disabling\n");
    r->present = false;
    return;
#else
    /* On the BSDs MSG_NOSIGNAL is a no-op (set to 0 above); ignore SIGPIPE
     * process-wide so a TCP peer hanging up mid-send can't terminate the
     * emulator. Harmless on Linux too. */
    signal(SIGPIPE, SIG_IGN);

    if (backend && !strcmp(backend, "tcp")) {
        r->backend = RS232_BACKEND_TCP;
        if (open_tcp(r, tcp_port) < 0) r->present = false;
    } else {
        r->backend = RS232_BACKEND_PTY;
        if (open_pty(r, pty_link_path) < 0) r->present = false;
    }
#endif
}

void rs232_shutdown(Rs232 *r) {
#if RS232_HAVE_HOST_BACKEND
    close_fd(&r->pty_master);
    close_fd(&r->tcp_client);
    close_fd(&r->tcp_listen);
    if (r->pty_link[0]) {
        unlink(r->pty_link);
        r->pty_link[0] = '\0';
    }
#endif
    r->present = false;
}

#if RS232_HAVE_HOST_BACKEND
static void poll_pty(Rs232 *r) {
    if (r->pty_master < 0) return;
    while (rsb_space(r->rx_head, r->rx_tail) > 0) {
        u8 c;
        ssize_t n = read(r->pty_master, &c, 1);
        if (n <= 0) break;
        rx_push(r, c);
    }
    u8 c;
    while (tx_pop(r, &c)) {
        ssize_t n = write(r->pty_master, &c, 1);
        if (n < 0) break;
    }
}

static void poll_tcp(Rs232 *r) {
    if (r->tcp_listen < 0) return;

    if (r->tcp_client < 0) {
        int fd = accept(r->tcp_listen, NULL, NULL);
        if (fd >= 0) {
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | (int)O_NONBLOCK);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            r->tcp_client = fd;
            fprintf(stderr, "rs232: TCP client connected\n");
        }
    }
    if (r->tcp_client < 0) return;

    bool read_eof = false;
    while (!read_eof && rsb_space(r->rx_head, r->rx_tail) > 0) {
        u8 c;
        ssize_t n = recv(r->tcp_client, &c, 1, 0);
        if (n == 0) { read_eof = true; break; }
        if (n < 0) break;
        rx_push(r, c);
    }
    u8 c;
    while (tx_pop(r, &c)) {
        ssize_t n = send(r->tcp_client, &c, 1, MSG_NOSIGNAL);
        if (n < 0) {
            close(r->tcp_client);
            r->tcp_client = -1;
            fprintf(stderr, "rs232: TCP client disconnected\n");
            return;
        }
    }
}
#endif /* host backend */

void rs232_poll(Rs232 *r) {
    if (!r->present) return;
#if RS232_HAVE_HOST_BACKEND
    if (r->backend == RS232_BACKEND_PTY) poll_pty(r);
    else                                 poll_tcp(r);
#else
    (void)r;
#endif
}