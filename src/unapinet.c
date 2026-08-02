#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include "unapinet.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NetSocket;
typedef int NetSockLen;
#define NET_INVALID INVALID_SOCKET
#define net_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetSocket;
typedef socklen_t NetSockLen;
#define NET_INVALID (-1)
#define net_close close
#endif

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TCP 4u
#define MAX_UDP 4u
#define MAX_TRANSFER 4096u
#define MAX_BUFFER 65536u
#define MAX_PARAMS (65536u + 16u)
#define UDP_PAYLOAD 2048u
#define UDP_QUEUE 16u

enum { ST_OK = 0, ST_ERROR = 1, ST_DATA = 2 };
enum { TCP_CLOSED = 0, TCP_LISTEN = 1, TCP_SYN_SENT = 2,
       TCP_ESTABLISHED = 4, TCP_FIN_WAIT = 5, TCP_CLOSE_WAIT = 7 };
enum { CR_NONE = 0, CR_NEVER = 1, CR_USER = 2, CR_ABORT = 3,
       CR_RESET = 4, CR_CONNECT = 6 };

typedef struct {
    NetSocket socket;
    u8 state, close_reason;
    bool resident, fin_sent;
    Uint64 close_deadline;
    uint32_t remote_ip;
    uint16_t remote_port, local_port;
    u8 recv_data[MAX_BUFFER], send_data[MAX_BUFFER];
    size_t recv_size, send_size;
} TcpConnection;

typedef struct {
    uint32_t ip;
    uint16_t port, size;
    u8 data[UDP_PAYLOAD];
} UdpDatagram;

typedef struct {
    NetSocket socket;
    uint16_t local_port;
    UdpDatagram queue[UDP_QUEUE];
    unsigned head, count;
} UdpConnection;

struct UnapiNet {
    bool enabled, sockets_ready;
    atomic_bool activity;
    char error[192];
    u8 status, params[MAX_PARAMS], result[MAX_TRANSFER + 16];
    size_t params_size, result_size, result_pos;
    TcpConnection tcp[MAX_TCP];
    UdpConnection udp[MAX_UDP];
    SDL_Mutex *dns_mutex;
    SDL_Thread *dns_thread;
    int dns_state;
    uint32_t dns_ip;
    unsigned dns_generation;
};

typedef struct {
    UnapiNet *net;
    unsigned generation;
    char hostname[];
} DnsJob;

static uint16_t get16(const u8 *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const u8 *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void put16(u8 *p, uint16_t v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, uint32_t v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
static struct sockaddr_in address4(uint32_t ip, uint16_t port) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons(port); return a;
}
static bool would_block(void) {
#ifdef _WIN32
    int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
static bool connecting(void) {
#ifdef _WIN32
    int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK;
#endif
}

static int net_send(NetSocket socket, const u8 *data, size_t size) {
#ifdef MSG_NOSIGNAL
    return send(socket, (const char *)data, (int)size, MSG_NOSIGNAL);
#else
    return send(socket, (const char *)data, (int)size, 0);
#endif
}

static void nonblocking(NetSocket s) {
#ifdef _WIN32
    u_long value = 1; (void)ioctlsocket(s, FIONBIO, &value);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}
static void option(NetSocket s, int level, int name) {
    int value = 1;
    (void)setsockopt(s, level, name, (const char *)&value, sizeof(value));
}
static void answer(UnapiNet *n, const u8 *data, size_t size) {
    if (size > sizeof(n->result)) size = sizeof(n->result);
    if (size) memmove(n->result, data, size);
    n->result_size = size; n->result_pos = 0; n->status = ST_DATA;
}
static void answer_byte(UnapiNet *n, u8 value) { answer(n, &value, 1); }
static void protocol_error(UnapiNet *n) {
    n->result_size = n->result_pos = 0; n->status = ST_ERROR;
}
static void close_tcp(TcpConnection *c, u8 reason, bool clear) {
    if (c->socket != NET_INVALID) { net_close(c->socket); c->socket = NET_INVALID; }
    c->state = TCP_CLOSED; c->close_reason = reason;
    c->send_size = 0; c->fin_sent = false;
    if (clear) {
        c->remote_ip = 0; c->remote_port = c->local_port = 0;
        c->resident = false; c->recv_size = 0;
    }
}
static void close_udp(UdpConnection *u) {
    if (u->socket != NET_INVALID) { net_close(u->socket); u->socket = NET_INVALID; }
    u->local_port = 0; u->head = u->count = 0;
}

static int dns_worker(void *context) {
    DnsJob *job = context;
    struct addrinfo hints, *addresses = NULL;
    uint32_t ip = 0;
    bool success = false;
    memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (!getaddrinfo(job->hostname, NULL, &hints, &addresses) && addresses) {
        ip = ntohl(((struct sockaddr_in *)addresses->ai_addr)->sin_addr.s_addr);
        success = true;
    }
    if (addresses) freeaddrinfo(addresses);
    SDL_LockMutex(job->net->dns_mutex);
    if (job->generation == job->net->dns_generation) {
        job->net->dns_ip = ip; job->net->dns_state = success ? 2 : 3;
        job->net->activity = true;
    }
    SDL_UnlockMutex(job->net->dns_mutex);
    free(job); return 0;
}

UnapiNet *unapinet_create(void) {
    UnapiNet *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    atomic_init(&n->activity, false);
    n->dns_mutex = SDL_CreateMutex();
    if (!n->dns_mutex) { free(n); return NULL; }
    for (unsigned i = 0; i < MAX_TCP; ++i) {
        n->tcp[i].socket = NET_INVALID; n->tcp[i].close_reason = CR_NEVER;
    }
    for (unsigned i = 0; i < MAX_UDP; ++i) n->udp[i].socket = NET_INVALID;
    return n;
}
void unapinet_reset(UnapiNet *n) {
    if (!n) return;
    for (unsigned i = 0; i < MAX_TCP; ++i) close_tcp(&n->tcp[i], CR_NEVER, true);
    for (unsigned i = 0; i < MAX_UDP; ++i) close_udp(&n->udp[i]);
    n->status = ST_OK; n->params_size = n->result_size = n->result_pos = 0;
    SDL_LockMutex(n->dns_mutex); ++n->dns_generation;
    n->dns_state = 0; n->dns_ip = 0; SDL_UnlockMutex(n->dns_mutex);
    n->activity = false;
}
bool unapinet_set_enabled(UnapiNet *n, bool enabled) {
    if (!n) return !enabled;
    if (enabled && !n->sockets_ready) {
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data)) {
            snprintf(n->error, sizeof(n->error), "Winsock 2 initialization failed");
            return false;
        }
#endif
        n->sockets_ready = true;
    }
    if (!enabled) unapinet_reset(n);
    n->enabled = enabled; n->error[0] = '\0'; return true;
}
bool unapinet_enabled(const UnapiNet *n) { return n && n->enabled; }
void unapinet_destroy(UnapiNet *n) {
    if (!n) return;
    if (n->dns_thread) {
        SDL_LockMutex(n->dns_mutex); ++n->dns_generation;
        SDL_UnlockMutex(n->dns_mutex);
        SDL_WaitThread(n->dns_thread, NULL);
        n->dns_thread = NULL;
    }
    unapinet_reset(n);
#ifdef _WIN32
    if (n->sockets_ready) WSACleanup();
#endif
    SDL_DestroyMutex(n->dns_mutex); free(n);
}

static void poll_connect(UnapiNet *n, TcpConnection *c) {
    fd_set write_set, error_set; struct timeval tv = {0, 0}; int error = 0;
    NetSockLen size = (NetSockLen)sizeof(error);
    FD_ZERO(&write_set); FD_ZERO(&error_set);
    FD_SET(c->socket, &write_set); FD_SET(c->socket, &error_set);
    if (select((int)c->socket + 1, NULL, &write_set, &error_set, &tv) <= 0) return;
    if (FD_ISSET(c->socket, &error_set) ||
        getsockopt(c->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &size) || error)
        close_tcp(c, CR_CONNECT, false);
    else if (FD_ISSET(c->socket, &write_set)) { c->state = TCP_ESTABLISHED; n->activity = true; }
}

static void poll_tcp(UnapiNet *n, TcpConnection *c) {
    if (c->socket == NET_INVALID) return;
    if (c->state == TCP_SYN_SENT) { poll_connect(n, c); return; }
    if (c->state == TCP_LISTEN) {
        struct sockaddr_in peer; NetSockLen size = (NetSockLen)sizeof(peer);
        NetSocket accepted = accept(c->socket, (struct sockaddr *)&peer, &size);
        if (accepted != NET_INVALID) {
            uint32_t ip = ntohl(peer.sin_addr.s_addr);
            if (c->remote_ip && c->remote_ip != ip) net_close(accepted);
            else {
                nonblocking(accepted); option(accepted, IPPROTO_TCP, TCP_NODELAY);
                net_close(c->socket); c->socket = accepted;
                c->state = TCP_ESTABLISHED; c->remote_ip = ip;
                c->remote_port = ntohs(peer.sin_port); n->activity = true;
            }
        }
        return;
    }
    if (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT &&
        c->state != TCP_FIN_WAIT) return;
    if (c->send_size) {
        int sent = net_send(c->socket, c->send_data, c->send_size);
        if (sent > 0) {
            memmove(c->send_data, c->send_data + sent, c->send_size - (size_t)sent);
            c->send_size -= (size_t)sent; n->activity = true;
        } else if (sent < 0 && !would_block()) { close_tcp(c, CR_RESET, false); return; }
    }
    if (c->state == TCP_FIN_WAIT) {
        if (!c->fin_sent && !c->send_size) {
#ifdef _WIN32
            (void)shutdown(c->socket, SD_SEND);
#else
            (void)shutdown(c->socket, SHUT_WR);
#endif
            c->fin_sent = true;
        }
        if (SDL_GetTicks() > c->close_deadline) { close_tcp(c, CR_USER, true); return; }
    }
    for (unsigned i = 0; i < 16 && c->recv_size < MAX_BUFFER; ++i) {
        size_t room = MAX_BUFFER - c->recv_size;
        int got = recv(c->socket, (char *)c->recv_data + c->recv_size,
                       (int)(room < 512 ? room : 512), 0);
        if (got > 0) { c->recv_size += (size_t)got; n->activity = true; continue; }
        if (!got) {
            if (c->state == TCP_FIN_WAIT) close_tcp(c, CR_USER, true);
            else c->state = TCP_CLOSE_WAIT;
        } else if (!would_block()) close_tcp(c, CR_RESET, false);
        break;
    }
}

static void poll_udp(UnapiNet *n, UdpConnection *u) {
    for (unsigned i = 0; u->socket != NET_INVALID && i < 16; ++i) {
        u8 data[UDP_PAYLOAD]; struct sockaddr_in source;
        NetSockLen size = (NetSockLen)sizeof(source);
        int got = recvfrom(u->socket, (char *)data, sizeof(data), 0,
                           (struct sockaddr *)&source, &size);
        if (got <= 0) break;
        n->activity = true;
        if (u->count < UDP_QUEUE) {
            UdpDatagram *d = &u->queue[(u->head + u->count) % UDP_QUEUE];
            d->ip = ntohl(source.sin_addr.s_addr); d->port = ntohs(source.sin_port);
            d->size = (uint16_t)got; memcpy(d->data, data, (size_t)got); ++u->count;
        }
    }
}

void unapinet_poll(UnapiNet *n) {
    if (!n || !n->enabled) return;
    for (unsigned i = 0; i < MAX_TCP; ++i) poll_tcp(n, &n->tcp[i]);
    for (unsigned i = 0; i < MAX_UDP; ++i) poll_udp(n, &n->udp[i]);
}

static TcpConnection *tcp_handle(UnapiNet *n, u8 h) {
    return h >= 1 && h <= MAX_TCP ? &n->tcp[h - 1] : NULL;
}
static UdpConnection *udp_handle(UnapiNet *n, u8 h) {
    return h >= 1 && h <= MAX_UDP ? &n->udp[h - 1] : NULL;
}
static int tcp_slot(UnapiNet *n) {
    for (unsigned i = 0; i < MAX_TCP; ++i)
        if (n->tcp[i].socket == NET_INVALID && n->tcp[i].state == TCP_CLOSED) return (int)i;
    return -1;
}
static int udp_slot(UnapiNet *n) {
    for (unsigned i = 0; i < MAX_UDP; ++i) if (n->udp[i].socket == NET_INVALID) return (int)i;
    return -1;
}

static void cmd_dns_query(UnapiNet *n) {
    size_t length = 0; struct in_addr direct; DnsJob *job;
    SDL_LockMutex(n->dns_mutex);
    if (n->dns_state == 1) {
        SDL_UnlockMutex(n->dns_mutex); protocol_error(n); return;
    }
    SDL_UnlockMutex(n->dns_mutex);
    while (length < n->params_size && n->params[length]) ++length;
    if (!length) { protocol_error(n); return; }
    job = malloc(sizeof(*job) + length + 1);
    if (!job) { protocol_error(n); return; }
    memcpy(job->hostname, n->params, length); job->hostname[length] = '\0';
    if (inet_pton(AF_INET, job->hostname, &direct) == 1) {
        u8 result[5] = {1, 0, 0, 0, 0};
        free(job); SDL_LockMutex(n->dns_mutex);
        n->dns_ip = ntohl(direct.s_addr); n->dns_state = 2;
        SDL_UnlockMutex(n->dns_mutex);
        put32(result + 1, ntohl(direct.s_addr));
        answer(n, result, sizeof(result)); return;
    }
    if (n->dns_thread) {
        SDL_WaitThread(n->dns_thread, NULL); n->dns_thread = NULL;
    }
    SDL_LockMutex(n->dns_mutex); ++n->dns_generation;
    n->dns_state = 1; n->dns_ip = 0; job->net = n;
    job->generation = n->dns_generation; SDL_UnlockMutex(n->dns_mutex);
    n->dns_thread = SDL_CreateThread(dns_worker, "1983-unapi-dns", job);
    if (!n->dns_thread) {
        free(job); SDL_LockMutex(n->dns_mutex); n->dns_state = 3;
        SDL_UnlockMutex(n->dns_mutex);
    }
    answer_byte(n, 0);
}
static void cmd_dns_status(UnapiNet *n) {
    int state; uint32_t ip;
    SDL_LockMutex(n->dns_mutex); state = n->dns_state; ip = n->dns_ip;
    SDL_UnlockMutex(n->dns_mutex);
    if (state == 2) {
        u8 result[5] = {2, 0, 0, 0, 0}; put32(result + 1, ip);
        answer(n, result, sizeof(result));
    } else if (state == 3) {
        static const u8 failed[2] = {0xff, 3}; answer(n, failed, sizeof(failed));
    } else answer_byte(n, (u8)state);
}

static void cmd_tcp_open(UnapiNet *n) {
    int index; NetSocket descriptor; struct sockaddr_in a; NetSockLen a_size;
    TcpConnection *c; uint32_t ip; uint16_t remote, local; u8 flags;
    if (n->params_size < 11 || (index = tcp_slot(n)) < 0) { answer_byte(n, 0); return; }
    ip = get32(n->params); remote = get16(n->params + 4);
    local = get16(n->params + 6); flags = n->params[10];
    descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (descriptor == NET_INVALID) { answer_byte(n, 0); return; }
    nonblocking(descriptor); option(descriptor, IPPROTO_TCP, TCP_NODELAY);
    c = &n->tcp[index]; close_tcp(c, CR_NEVER, true);
    c->close_reason = CR_NONE; c->remote_ip = ip; c->remote_port = remote;
    c->resident = (flags & 2) != 0;
    if (flags & 1) {
        if (local == 0xffff) local = 0;
        option(descriptor, SOL_SOCKET, SO_REUSEADDR); a = address4(INADDR_ANY, local);
        if (bind(descriptor, (struct sockaddr *)&a, sizeof(a)) || listen(descriptor, 1)) {
            net_close(descriptor); answer_byte(n, 0); return;
        }
        c->state = TCP_LISTEN;
    } else {
        a = address4(ip, remote);
        if (!connect(descriptor, (struct sockaddr *)&a, sizeof(a))) c->state = TCP_ESTABLISHED;
        else if (connecting()) c->state = TCP_SYN_SENT;
        else { net_close(descriptor); answer_byte(n, 0); return; }
    }
    c->socket = descriptor; a_size = (NetSockLen)sizeof(a);
    if (!getsockname(descriptor, (struct sockaddr *)&a, &a_size)) c->local_port = ntohs(a.sin_port);
    answer_byte(n, (u8)(index + 1));
}

static void cmd_tcp_send(UnapiNet *n) {
    TcpConnection *c; uint16_t length; const u8 *data; size_t sent = 0;
    if (n->params_size < 3) { answer_byte(n, 1); return; }
    c = tcp_handle(n, n->params[0]); length = get16(n->params + 1);
    if (!c || c->socket == NET_INVALID ||
        (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT) ||
        n->params_size < (size_t)length + 3) { answer_byte(n, 1); return; }
    data = n->params + 3;
    if (!c->send_size && length) {
        int result = net_send(c->socket, data, length);
        if (result > 0) { sent = (size_t)result; n->activity = true; }
        else if (result < 0 && !would_block()) {
            close_tcp(c, CR_RESET, false); answer_byte(n, 1); return;
        }
    }
    if (c->send_size + (size_t)length - sent > MAX_BUFFER) { answer_byte(n, 2); return; }
    memcpy(c->send_data + c->send_size, data + sent, (size_t)length - sent);
    c->send_size += (size_t)length - sent; answer_byte(n, 0);
}

static void cmd_tcp_recv(UnapiNet *n) {
    TcpConnection *c; uint16_t maximum; size_t length;
    if (n->params_size < 3) { static const u8 zero[2] = {0}; answer(n, zero, 2); return; }
    c = tcp_handle(n, n->params[0]); maximum = get16(n->params + 1);
    if (maximum > MAX_TRANSFER) maximum = MAX_TRANSFER;
    length = c ? (c->recv_size < maximum ? c->recv_size : maximum) : 0;
    put16(n->result, (uint16_t)length);
    if (length) {
        memcpy(n->result + 2, c->recv_data, length);
        memmove(c->recv_data, c->recv_data + length, c->recv_size - length);
        c->recv_size -= length;
    }
    n->result_size = length + 2; n->result_pos = 0; n->status = ST_DATA;
}
static void graceful_close(TcpConnection *c) {
    if (c->socket != NET_INVALID &&
        (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT)) {
        c->close_reason = CR_USER; c->state = TCP_FIN_WAIT;
        c->close_deadline = SDL_GetTicks() + 30000; c->fin_sent = false;
    } else close_tcp(c, CR_USER, true);
}
static void cmd_tcp_close(UnapiNet *n) {
    TcpConnection *c;
    if (!n->params_size) { answer_byte(n, 1); return; }
    if (!n->params[0]) {
        for (unsigned i = 0; i < MAX_TCP; ++i)
            if (!n->tcp[i].resident && n->tcp[i].socket != NET_INVALID)
                graceful_close(&n->tcp[i]);
        answer_byte(n, 0); return;
    }
    c = tcp_handle(n, n->params[0]);
    if (!c || c->socket == NET_INVALID) { answer_byte(n, 1); return; }
    graceful_close(c); answer_byte(n, 0);
}
static void cmd_tcp_state(UnapiNet *n) {
    u8 result[12] = {0};
    TcpConnection *c = n->params_size ? tcp_handle(n, n->params[0]) : NULL;
    result[3] = CR_NEVER;
    if (c) {
        result[0] = c->state;
        put16(result + 1, (uint16_t)(c->recv_size > 0xffff ? 0xffff : c->recv_size));
        result[3] = c->close_reason; put32(result + 4, c->remote_ip);
        put16(result + 8, c->remote_port); put16(result + 10, c->local_port);
    }
    answer(n, result, sizeof(result));
}
static void cmd_tcp_abort(UnapiNet *n) {
    TcpConnection *c;
    if (!n->params_size || !(c = tcp_handle(n, n->params[0])) ||
        c->socket == NET_INVALID) { answer_byte(n, 1); return; }
    close_tcp(c, CR_ABORT, true); answer_byte(n, 0);
}
static uint32_t local_ip(void) {
    NetSocket descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in remote = address4(0x08080808u, 80), local;
    NetSockLen size = (NetSockLen)sizeof(local); uint32_t ip = 0;
    if (descriptor == NET_INVALID) return 0;
    if (!connect(descriptor, (struct sockaddr *)&remote, sizeof(remote)) &&
        !getsockname(descriptor, (struct sockaddr *)&local, &size))
        ip = ntohl(local.sin_addr.s_addr);
    net_close(descriptor); return ip;
}

static void cmd_udp_open(UnapiNet *n) {
    int index; NetSocket descriptor; uint16_t port;
    struct sockaddr_in a; NetSockLen size; UdpConnection *u;
    if (n->params_size < 2 || (index = udp_slot(n)) < 0) { answer_byte(n, 0); return; }
    port = get16(n->params); descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (descriptor == NET_INVALID) { answer_byte(n, 0); return; }
    nonblocking(descriptor); option(descriptor, SOL_SOCKET, SO_BROADCAST);
    a = address4(INADDR_ANY, port == 0xffff ? 0 : port);
    if (bind(descriptor, (struct sockaddr *)&a, sizeof(a))) {
        a.sin_port = 0;
        if (bind(descriptor, (struct sockaddr *)&a, sizeof(a))) {
            net_close(descriptor); answer_byte(n, 0); return;
        }
    }
    u = &n->udp[index]; close_udp(u); u->socket = descriptor;
    size = (NetSockLen)sizeof(a);
    if (!getsockname(descriptor, (struct sockaddr *)&a, &size)) u->local_port = ntohs(a.sin_port);
    answer_byte(n, (u8)(index + 1));
}

static void cmd_udp_close(UnapiNet *n) {
    UdpConnection *u;
    if (!n->params_size) { answer_byte(n, 1); return; }
    if (!n->params[0]) {
        for (unsigned i = 0; i < MAX_UDP; ++i) close_udp(&n->udp[i]);
        answer_byte(n, 0); return;
    }
    u = udp_handle(n, n->params[0]);
    if (!u || u->socket == NET_INVALID) { answer_byte(n, 1); return; }
    close_udp(u); answer_byte(n, 0);
}
static void cmd_udp_state(UnapiNet *n) {
    UdpConnection *u = n->params_size ? udp_handle(n, n->params[0]) : NULL;
    u8 result[2]; uint16_t size = 0;
    if (u && u->socket != NET_INVALID && u->count) size = u->queue[u->head].size;
    put16(result, size); answer(n, result, sizeof(result));
}
static void cmd_udp_send(UnapiNet *n) {
    UdpConnection *u; uint16_t port, size; uint32_t ip;
    struct sockaddr_in destination; int sent;
    if (n->params_size < 9) { answer_byte(n, 1); return; }
    u = udp_handle(n, n->params[0]); ip = get32(n->params + 1);
    port = get16(n->params + 5); size = get16(n->params + 7);
    if (!u || u->socket == NET_INVALID || n->params_size < (size_t)size + 9) {
        answer_byte(n, 1); return;
    }
    destination = address4(ip, port);
    sent = sendto(u->socket, (const char *)n->params + 9, size, 0,
                  (struct sockaddr *)&destination, sizeof(destination));
    if (sent == size) n->activity = true;
    answer_byte(n, sent == size ? 0 : 1);
}
static void cmd_udp_recv(UnapiNet *n) {
    UdpConnection *u; UdpDatagram *d; uint16_t maximum, actual;
    static const u8 empty[8] = {0};
    if (n->params_size < 3) { answer(n, empty, sizeof(empty)); return; }
    u = udp_handle(n, n->params[0]); maximum = get16(n->params + 1);
    if (!u || u->socket == NET_INVALID || !u->count) {
        answer(n, empty, sizeof(empty)); return;
    }
    d = &u->queue[u->head]; actual = d->size < maximum ? d->size : maximum;
    put32(n->result, d->ip); put16(n->result + 4, d->port);
    put16(n->result + 6, actual); memcpy(n->result + 8, d->data, actual);
    n->result_size = 8u + actual; n->result_pos = 0; n->status = ST_DATA;
    u->head = (u->head + 1) % UDP_QUEUE; --u->count;
}

static void process_command(UnapiNet *n, u8 command) {
    unapinet_poll(n);
    switch (command) {
        case 0x00: answer_byte(n, 0xab); break;
        case 0x01: cmd_dns_query(n); break;
        case 0x02: cmd_dns_status(n); break;
        case 0x03: cmd_tcp_open(n); break;
        case 0x04: cmd_tcp_send(n); break;
        case 0x05: cmd_tcp_recv(n); break;
        case 0x06: cmd_tcp_close(n); break;
        case 0x07: cmd_tcp_state(n); break;
        case 0x08: cmd_tcp_abort(n); break;
        case 0x09: cmd_udp_open(n); break;
        case 0x0a: cmd_udp_close(n); break;
        case 0x0b: cmd_udp_state(n); break;
        case 0x0c: cmd_udp_send(n); break;
        case 0x0d: put32(n->result, local_ip()); answer(n, n->result, 4); break;
        case 0x0e: answer_byte(n, 2); break;
        case 0x0f: cmd_udp_recv(n); break;
        case 0x10: { static const u8 caps[2] = {0x0f, 0x04}; answer(n, caps, 2); break; }
        /* openMSXnet v1 acknowledges ICMP on non-Windows but reports no reply. */
        case 0x11: answer_byte(n, n->params_size < 11 ? 1 : 0); break;
        case 0x12: answer_byte(n, 0); break;
        default: protocol_error(n); break;
    }
    n->params_size = 0;
}

bool unapinet_io_read(void *context, u16 port, u8 *value) {
    UnapiNet *n = context; u8 low = (u8)port;
    if (!n || !n->enabled || !value ||
        (low != UNAPINET_COMMAND_PORT && low != UNAPINET_DATA_PORT)) return false;
    if (low == UNAPINET_COMMAND_PORT) *value = n->status;
    else if (n->status == ST_DATA && n->result_pos < n->result_size) {
        *value = n->result[n->result_pos++];
        if (n->result_pos >= n->result_size) n->status = ST_OK;
    } else *value = 0;
    return true;
}
bool unapinet_io_write(void *context, u16 port, u8 value) {
    UnapiNet *n = context; u8 low = (u8)port;
    if (!n || !n->enabled ||
        (low != UNAPINET_COMMAND_PORT && low != UNAPINET_DATA_PORT)) return false;
    if (low == UNAPINET_DATA_PORT) {
        if (n->status == ST_DATA) { n->status = ST_OK; n->result_size = n->result_pos = 0; }
        if (n->params_size < sizeof(n->params)) n->params[n->params_size++] = value;
    } else process_command(n, value);
    return true;
}
void unapinet_io_reset(void *context) { unapinet_reset(context); }
bool unapinet_take_activity(UnapiNet *n) {
    if (!n) return false;
    return atomic_exchange(&n->activity, false);
}
const char *unapinet_error(const UnapiNet *n) {
    return n && n->error[0] ? n->error : "no error";
}
