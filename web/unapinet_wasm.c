#include "unapinet.h"
#include "unapinet_web.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TCP 4u
#define MAX_UDP 4u
#define MAX_TRANSFER 4096u
#define MAX_PARAMS (65536u + 16u)

enum { ST_OK = 0, ST_ERROR = 1, ST_DATA = 2 };
enum { TCP_CLOSED = 0, TCP_LISTEN = 1, TCP_SYN_SENT = 2,
       TCP_ESTABLISHED = 4, TCP_FIN_WAIT = 5, TCP_CLOSE_WAIT = 7 };
enum { CR_NONE = 0, CR_NEVER = 1, CR_USER = 2, CR_ABORT = 3,
       CR_RESET = 4, CR_CONNECT = 6 };

typedef struct {
    bool used, pending, resident;
    u8 state, close_reason;
    uint32_t remote_ip;
    uint16_t remote_port, local_port;
} WebTcp;

typedef struct {
    bool used, pending;
    uint16_t local_port;
} WebUdp;

struct UnapiNet {
    bool enabled, guest_driver_active;
    atomic_bool activity;
    char error[192];
    u8 status, params[MAX_PARAMS], result[MAX_TRANSFER + 16];
    size_t params_size, result_size, result_pos;
    WebTcp tcp[MAX_TCP];
    WebUdp udp[MAX_UDP];
    int dns_state;
    uint32_t dns_ip;
};

static uint16_t get16(const u8 *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const u8 *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put16(u8 *p, uint16_t value) {
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static void put32(u8 *p, uint32_t value) {
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}

static void answer(UnapiNet *net, const u8 *data, size_t size) {
    if (size > sizeof(net->result))
        size = sizeof(net->result);
    if (size)
        memmove(net->result, data, size);
    net->result_size = size;
    net->result_pos = 0;
    net->status = ST_DATA;
}

static void answer_byte(UnapiNet *net, u8 value) {
    answer(net, &value, 1);
}

static void protocol_error(UnapiNet *net) {
    net->result_size = 0;
    net->result_pos = 0;
    net->status = ST_ERROR;
}

static void clear_tcp(UnapiNet *net, unsigned slot,
                      u8 reason, bool clear_address) {
    WebTcp *connection = &net->tcp[slot];

    if (connection->used || connection->pending)
        unapinet_web_tcp_close((int)slot);
    connection->used = false;
    connection->pending = false;
    connection->state = TCP_CLOSED;
    connection->close_reason = reason;
    if (clear_address) {
        connection->resident = false;
        connection->remote_ip = 0;
        connection->remote_port = 0;
        connection->local_port = 0;
    }
}

static void clear_udp(UnapiNet *net, unsigned slot) {
    WebUdp *connection = &net->udp[slot];

    if (connection->used || connection->pending)
        unapinet_web_udp_close((int)slot);
    memset(connection, 0, sizeof(*connection));
}

UnapiNet *unapinet_create(void) {
    UnapiNet *net = calloc(1, sizeof(*net));

    if (!net)
        return NULL;
    atomic_init(&net->activity, false);
    for (unsigned slot = 0; slot < MAX_TCP; ++slot)
        net->tcp[slot].close_reason = CR_NEVER;
    return net;
}

void unapinet_reset(UnapiNet *net) {
    if (!net)
        return;
    unapinet_web_reset_channels();
    for (unsigned slot = 0; slot < MAX_TCP; ++slot) {
        memset(&net->tcp[slot], 0, sizeof(net->tcp[slot]));
        net->tcp[slot].close_reason = CR_NEVER;
    }
    memset(net->udp, 0, sizeof(net->udp));
    net->status = ST_OK;
    net->params_size = 0;
    net->result_size = 0;
    net->result_pos = 0;
    net->guest_driver_active = false;
    net->dns_state = 0;
    net->dns_ip = 0;
    net->activity = false;
}

bool unapinet_set_enabled(UnapiNet *net, bool enabled) {
    if (!net)
        return !enabled;
    if (!enabled)
        unapinet_reset(net);
    net->enabled = enabled;
    net->error[0] = '\0';
    unapinet_web_set_device(enabled ? 1 : 0);
    return true;
}

bool unapinet_enabled(const UnapiNet *net) {
    return net && net->enabled;
}

bool unapinet_guest_driver_active(const UnapiNet *net) {
    return net && net->enabled && net->guest_driver_active;
}

void unapinet_destroy(UnapiNet *net) {
    if (!net)
        return;
    (void)unapinet_set_enabled(net, false);
    free(net);
}

void unapinet_poll(UnapiNet *net) {
    if (!net || !net->enabled)
        return;
    if (unapinet_web_take_activity())
        net->activity = true;
    for (unsigned slot = 0; slot < MAX_TCP; ++slot) {
        int available;
        WebTcp *connection = &net->tcp[slot];

        if (!connection->used || connection->pending)
            continue;
        available = unapinet_web_tcp_available((int)slot);
        if (available == -1 && connection->state == TCP_ESTABLISHED)
            connection->state = TCP_CLOSE_WAIT;
        else if (available < -1)
            clear_tcp(net, slot, CR_RESET, false);
    }
}

static int tcp_slot(const UnapiNet *net) {
    for (unsigned slot = 0; slot < MAX_TCP; ++slot)
        if (!net->tcp[slot].used && !net->tcp[slot].pending)
            return (int)slot;
    return -1;
}

static int udp_slot(const UnapiNet *net) {
    for (unsigned slot = 0; slot < MAX_UDP; ++slot)
        if (!net->udp[slot].used && !net->udp[slot].pending)
            return (int)slot;
    return -1;
}

static WebTcp *tcp_handle(UnapiNet *net, u8 handle) {
    return handle >= 1 && handle <= MAX_TCP
         ? &net->tcp[handle - 1] : NULL;
}

static WebUdp *udp_handle(UnapiNet *net, u8 handle) {
    return handle >= 1 && handle <= MAX_UDP
         ? &net->udp[handle - 1] : NULL;
}

static void cmd_dns_query(UnapiNet *net) {
    char hostname[254];
    size_t length = 0;

    if (net->dns_state == 1) {
        protocol_error(net);
        return;
    }
    while (length < net->params_size && net->params[length])
        ++length;
    if (!length || length >= sizeof(hostname)) {
        protocol_error(net);
        return;
    }
    memcpy(hostname, net->params, length);
    hostname[length] = '\0';
    net->dns_state = 1;
    net->dns_ip = 0;
    if (!unapinet_web_dns(hostname))
        net->dns_state = 3;
    answer_byte(net, 0);
}

static void cmd_dns_status(UnapiNet *net) {
    if (net->dns_state == 2) {
        u8 result[5] = {2, 0, 0, 0, 0};
        put32(result + 1, net->dns_ip);
        answer(net, result, sizeof(result));
    } else if (net->dns_state == 3) {
        static const u8 failed[2] = {0xff, 3};
        answer(net, failed, sizeof(failed));
    } else {
        answer_byte(net, (u8)net->dns_state);
    }
}

static void cmd_tcp_open(UnapiNet *net) {
    int index;
    WebTcp *connection;
    uint32_t ip;
    uint16_t remote, local;
    u8 flags;
    char host[16];

    if (net->params_size < 11 || (index = tcp_slot(net)) < 0) {
        answer_byte(net, 0);
        return;
    }
    ip = get32(net->params);
    remote = get16(net->params + 4);
    local = get16(net->params + 6);
    flags = net->params[10];
    /* A browser relay cannot safely expose a listening socket. Client TCP is
     * sufficient for GeoBench, SymbOS, and normal TCP/IP UNAPI applications. */
    if ((flags & 1) || !ip || !remote || (local != 0 && local != 0xffff)) {
        answer_byte(net, 0);
        return;
    }
    snprintf(host, sizeof(host), "%u.%u.%u.%u",
             (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 0xff),
             (unsigned)((ip >> 8) & 0xff), (unsigned)(ip & 0xff));
    connection = &net->tcp[index];
    memset(connection, 0, sizeof(*connection));
    connection->pending = true;
    connection->state = TCP_SYN_SENT;
    connection->close_reason = CR_NONE;
    connection->remote_ip = ip;
    connection->remote_port = remote;
    connection->resident = (flags & 2) != 0;
    if (!unapinet_web_tcp_open(index, host, remote, 1)) {
        memset(connection, 0, sizeof(*connection));
        connection->close_reason = CR_CONNECT;
        answer_byte(net, 0);
        return;
    }
    answer_byte(net, (u8)(index + 1));
}

static void cmd_tcp_send(UnapiNet *net) {
    WebTcp *connection;
    uint16_t length;

    if (net->params_size < 3) {
        answer_byte(net, 1);
        return;
    }
    connection = tcp_handle(net, net->params[0]);
    length = get16(net->params + 1);
    if (!connection || !connection->used ||
        (connection->state != TCP_ESTABLISHED &&
         connection->state != TCP_CLOSE_WAIT) ||
        net->params_size < (size_t)length + 3) {
        answer_byte(net, 1);
        return;
    }
    if (length && !unapinet_web_tcp_send(
            (int)(connection - net->tcp), net->params + 3, length)) {
        answer_byte(net, 2);
        return;
    }
    if (length)
        net->activity = true;
    answer_byte(net, 0);
}

static void cmd_tcp_recv(UnapiNet *net) {
    WebTcp *connection;
    uint16_t maximum;
    int length = 0;

    if (net->params_size < 3) {
        static const u8 empty[2] = {0};
        answer(net, empty, sizeof(empty));
        return;
    }
    connection = tcp_handle(net, net->params[0]);
    maximum = get16(net->params + 1);
    if (maximum > MAX_TRANSFER)
        maximum = MAX_TRANSFER;
    if (connection && (connection->used || connection->pending))
        length = unapinet_web_tcp_read(
            (int)(connection - net->tcp), net->result + 2, maximum);
    if (length < 0)
        length = 0;
    put16(net->result, (uint16_t)length);
    net->result_size = (size_t)length + 2;
    net->result_pos = 0;
    net->status = ST_DATA;
    if (length)
        net->activity = true;
}

static void cmd_tcp_close(UnapiNet *net) {
    WebTcp *connection;

    if (!net->params_size) {
        answer_byte(net, 1);
        return;
    }
    if (!net->params[0]) {
        for (unsigned slot = 0; slot < MAX_TCP; ++slot)
            if (!net->tcp[slot].resident &&
                (net->tcp[slot].used || net->tcp[slot].pending))
                clear_tcp(net, slot, CR_USER, true);
        answer_byte(net, 0);
        return;
    }
    connection = tcp_handle(net, net->params[0]);
    if (!connection || (!connection->used && !connection->pending)) {
        answer_byte(net, 1);
        return;
    }
    clear_tcp(net, (unsigned)(connection - net->tcp), CR_USER, true);
    answer_byte(net, 0);
}

static void cmd_tcp_state(UnapiNet *net) {
    u8 result[12] = {0};
    WebTcp *connection = net->params_size
                       ? tcp_handle(net, net->params[0]) : NULL;

    result[3] = CR_NEVER;
    if (connection) {
        int available = (connection->used || connection->pending)
                      ? unapinet_web_tcp_available(
                            (int)(connection - net->tcp)) : 0;
        if (available < 0)
            available = 0;
        result[0] = connection->state;
        put16(result + 1, (uint16_t)(available > 0xffff ? 0xffff : available));
        result[3] = connection->close_reason;
        put32(result + 4, connection->remote_ip);
        put16(result + 8, connection->remote_port);
        put16(result + 10, connection->local_port);
    }
    answer(net, result, sizeof(result));
}

static void cmd_tcp_abort(UnapiNet *net) {
    WebTcp *connection;

    if (!net->params_size ||
        !(connection = tcp_handle(net, net->params[0])) ||
        (!connection->used && !connection->pending)) {
        answer_byte(net, 1);
        return;
    }
    clear_tcp(net, (unsigned)(connection - net->tcp), CR_ABORT, true);
    answer_byte(net, 0);
}

static void cmd_udp_open(UnapiNet *net) {
    int index;
    uint16_t port;
    WebUdp *connection;

    if (net->params_size < 2 || (index = udp_slot(net)) < 0) {
        answer_byte(net, 0);
        return;
    }
    port = get16(net->params);
    if (port == 0xffff)
        port = 0;
    if (port != 0) {
        answer_byte(net, 0);
        return;
    }
    connection = &net->udp[index];
    memset(connection, 0, sizeof(*connection));
    connection->pending = true;
    if (!unapinet_web_udp_open(index, port)) {
        memset(connection, 0, sizeof(*connection));
        answer_byte(net, 0);
        return;
    }
    answer_byte(net, (u8)(index + 1));
}

static void cmd_udp_close(UnapiNet *net) {
    WebUdp *connection;

    if (!net->params_size) {
        answer_byte(net, 1);
        return;
    }
    if (!net->params[0]) {
        for (unsigned slot = 0; slot < MAX_UDP; ++slot)
            clear_udp(net, slot);
        answer_byte(net, 0);
        return;
    }
    connection = udp_handle(net, net->params[0]);
    if (!connection || (!connection->used && !connection->pending)) {
        answer_byte(net, 1);
        return;
    }
    clear_udp(net, (unsigned)(connection - net->udp));
    answer_byte(net, 0);
}

static void cmd_udp_state(UnapiNet *net) {
    WebUdp *connection = net->params_size
                       ? udp_handle(net, net->params[0]) : NULL;
    u8 result[2];
    int available = 0;

    if (connection && (connection->used || connection->pending))
        available = unapinet_web_udp_available(
            (int)(connection - net->udp));
    if (available < 0)
        available = 0;
    put16(result, (uint16_t)(available > 0xffff ? 0xffff : available));
    answer(net, result, sizeof(result));
}

static void cmd_udp_send(UnapiNet *net) {
    WebUdp *connection;
    uint16_t port, size;

    if (net->params_size < 9) {
        answer_byte(net, 1);
        return;
    }
    connection = udp_handle(net, net->params[0]);
    port = get16(net->params + 5);
    size = get16(net->params + 7);
    if (!connection || !connection->used ||
        net->params_size < (size_t)size + 9 ||
        !unapinet_web_udp_send(
            (int)(connection - net->udp), net->params + 1,
            port, net->params + 9, size)) {
        answer_byte(net, 1);
        return;
    }
    if (size)
        net->activity = true;
    answer_byte(net, 0);
}

static void cmd_udp_recv(UnapiNet *net) {
    WebUdp *connection;
    uint16_t maximum, port = 0;
    u8 address[4] = {0};
    int length = 0;

    if (net->params_size < 3) {
        static const u8 empty[8] = {0};
        answer(net, empty, sizeof(empty));
        return;
    }
    connection = udp_handle(net, net->params[0]);
    maximum = get16(net->params + 1);
    if (maximum > MAX_TRANSFER)
        maximum = MAX_TRANSFER;
    if (connection && connection->used)
        length = unapinet_web_udp_read(
            (int)(connection - net->udp), address, &port,
            net->result + 8, maximum);
    if (length < 0)
        length = 0;
    memcpy(net->result, address, sizeof(address));
    put16(net->result + 4, port);
    put16(net->result + 6, (uint16_t)length);
    net->result_size = (size_t)length + 8;
    net->result_pos = 0;
    net->status = ST_DATA;
    if (length)
        net->activity = true;
}

static void process_command(UnapiNet *net, u8 command) {
    unapinet_poll(net);
    switch (command) {
        case 0x00:
            net->guest_driver_active = true;
            answer_byte(net, 0xab);
            break;
        case 0x01: cmd_dns_query(net); break;
        case 0x02: cmd_dns_status(net); break;
        case 0x03: cmd_tcp_open(net); break;
        case 0x04: cmd_tcp_send(net); break;
        case 0x05: cmd_tcp_recv(net); break;
        case 0x06: cmd_tcp_close(net); break;
        case 0x07: cmd_tcp_state(net); break;
        case 0x08: cmd_tcp_abort(net); break;
        case 0x09: cmd_udp_open(net); break;
        case 0x0a: cmd_udp_close(net); break;
        case 0x0b: cmd_udp_state(net); break;
        case 0x0c: cmd_udp_send(net); break;
        case 0x0d:
            put32(net->result, 0xc0a8c603u); /* 192.168.198.3 */
            answer(net, net->result, 4);
            break;
        case 0x0e: answer_byte(net, 2); break;
        case 0x0f: cmd_udp_recv(net); break;
        case 0x10: {
            static const u8 capabilities[2] = {0x0f, 0x04};
            answer(net, capabilities, sizeof(capabilities));
            break;
        }
        case 0x11: answer_byte(net, net->params_size < 11 ? 1 : 0); break;
        case 0x12: answer_byte(net, 0); break;
        default: protocol_error(net); break;
    }
    net->params_size = 0;
}

bool unapinet_io_read(void *context, u16 port, u8 *value) {
    UnapiNet *net = context;
    u8 low = (u8)port;

    if (!net || !net->enabled || !value ||
        (low != UNAPINET_COMMAND_PORT && low != UNAPINET_DATA_PORT))
        return false;
    if (low == UNAPINET_COMMAND_PORT) {
        *value = net->status;
    } else if (net->status == ST_DATA && net->result_pos < net->result_size) {
        *value = net->result[net->result_pos++];
        if (net->result_pos >= net->result_size)
            net->status = ST_OK;
    } else {
        *value = 0;
    }
    return true;
}

bool unapinet_io_write(void *context, u16 port, u8 value) {
    UnapiNet *net = context;
    u8 low = (u8)port;

    if (!net || !net->enabled ||
        (low != UNAPINET_COMMAND_PORT && low != UNAPINET_DATA_PORT))
        return false;
    if (low == UNAPINET_DATA_PORT) {
        if (net->status == ST_DATA) {
            net->status = ST_OK;
            net->result_size = 0;
            net->result_pos = 0;
        }
        if (net->params_size < sizeof(net->params))
            net->params[net->params_size++] = value;
    } else {
        process_command(net, value);
    }
    return true;
}

void unapinet_io_reset(void *context) {
    unapinet_reset(context);
}

bool unapinet_take_activity(UnapiNet *net) {
    return net && atomic_exchange(&net->activity, false);
}

const char *unapinet_error(const UnapiNet *net) {
    return net && net->error[0] ? net->error : "no error";
}

void unapinet_web_dns_result(UnapiNet *net, u8 status,
                             const u8 address[4]) {
    if (!net || !net->enabled || net->dns_state != 1)
        return;
    if (status == 0) {
        net->dns_ip = get32(address);
        net->dns_state = 2;
    } else {
        net->dns_ip = 0;
        net->dns_state = 3;
    }
    net->activity = true;
}

void unapinet_web_tcp_open_result(UnapiNet *net, int slot, u8 status,
                                  const u8 address[4], u16 port) {
    WebTcp *connection;

    (void)address;
    if (!net || !net->enabled || slot < 0 || slot >= (int)MAX_TCP)
        return;
    connection = &net->tcp[slot];
    if (!connection->pending)
        return;
    connection->pending = false;
    if (status == 0) {
        connection->used = true;
        connection->state = TCP_ESTABLISHED;
        connection->local_port = port;
        connection->close_reason = CR_NONE;
    } else {
        connection->used = false;
        connection->state = TCP_CLOSED;
        connection->close_reason = CR_CONNECT;
    }
    net->activity = true;
}

void unapinet_web_udp_open_result(UnapiNet *net, int slot, u8 status,
                                  u16 port) {
    WebUdp *connection;

    if (!net || !net->enabled || slot < 0 || slot >= (int)MAX_UDP)
        return;
    connection = &net->udp[slot];
    if (!connection->pending)
        return;
    connection->pending = false;
    if (status == 0) {
        connection->used = true;
        connection->local_port = port;
    } else {
        connection->used = false;
        connection->local_port = 0;
    }
    net->activity = true;
}
