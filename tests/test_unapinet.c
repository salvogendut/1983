#include "unapinet.h"

#include <SDL3/SDL.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static void write_parameters(UnapiNet *net, const u8 *data, size_t size) {
    for (size_t i = 0; i < size; ++i)
        assert(unapinet_io_write(net, UNAPINET_DATA_PORT, data[i]));
}

static void command(UnapiNet *net, u8 value) {
    assert(unapinet_io_write(net, UNAPINET_COMMAND_PORT, value));
}

static u8 read_port(UnapiNet *net, u16 port) {
    u8 value = 0xff;
    assert(unapinet_io_read(net, port, &value));
    return value;
}

static void expect_result(UnapiNet *net, const u8 *expected, size_t size) {
    assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
    for (size_t i = 0; i < size; ++i)
        assert(read_port(net, UNAPINET_DATA_PORT) == expected[i]);
    assert(read_port(net, UNAPINET_COMMAND_PORT) == 0);
}

static void test_wire_protocol(UnapiNet *net) {
    static const u8 ping[] = {0xab};
    static const u8 capabilities[] = {0x0f, 0x04};
    static const u8 direct_dns[] = {1, 127, 0, 0, 1};
    static const u8 hostname[] = "127.0.0.1";
    u8 value = 0x5a;

    assert(!unapinet_io_read(net, UNAPINET_COMMAND_PORT, &value));
    assert(value == 0x5a);
    assert(unapinet_set_enabled(net, true));

    command(net, 0x00);
    expect_result(net, ping, sizeof(ping));
    command(net, 0x10);
    expect_result(net, capabilities, sizeof(capabilities));

    write_parameters(net, hostname, sizeof(hostname));
    command(net, 0x01);
    expect_result(net, direct_dns, sizeof(direct_dns));

    command(net, 0xfe);
    assert(read_port(net, UNAPINET_COMMAND_PORT) == 1);
    assert(read_port(net, UNAPINET_DATA_PORT) == 0);

    command(net, 0x00);
    assert(read_port(net, UNAPINET_DATA_PORT) == 0xab);
    write_parameters(net, (const u8 *)"x", 1);
    command(net, 0x00);
    expect_result(net, ping, sizeof(ping));

    unapinet_reset(net);
    assert(read_port(net, UNAPINET_COMMAND_PORT) == 0);
}

static void test_async_dns(UnapiNet *net) {
    static const u8 hostname[] = "localhost";
    u8 status = 0;

    write_parameters(net, hostname, sizeof(hostname));
    command(net, 0x01);
    assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
    status = read_port(net, UNAPINET_DATA_PORT);
    assert(status == 0);
    for (unsigned i = 0; i < 2000; ++i) {
        command(net, 0x02);
        assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
        status = read_port(net, UNAPINET_DATA_PORT);
        if (status == 2) {
            u8 address[4];

            for (unsigned j = 0; j < sizeof(address); ++j)
                address[j] = read_port(net, UNAPINET_DATA_PORT);
            assert(address[0] == 127);
            return;
        }
        assert(status == 1);
        SDL_Delay(1);
    }
    assert(!"timed out resolving localhost");
}

#ifndef _WIN32
static void set_nonblocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0);
}

static int loopback_socket(int type, uint16_t *port) {
    int socket_fd = socket(AF_INET, type, 0);
    struct sockaddr_in address;
    socklen_t length = sizeof(address);

    assert(socket_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(socket_fd, (struct sockaddr *)&address,
                sizeof(address)) == 0);
    assert(getsockname(socket_fd, (struct sockaddr *)&address,
                       &length) == 0);
    *port = ntohs(address.sin_port);
    set_nonblocking(socket_fd);
    return socket_fd;
}

static int wait_for_accept(UnapiNet *net, int listener) {
    for (unsigned i = 0; i < 2000; ++i) {
        int accepted;

        unapinet_poll(net);
        accepted = accept(listener, NULL, NULL);
        if (accepted >= 0) {
            set_nonblocking(accepted);
            return accepted;
        }
        assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
        SDL_Delay(1);
    }
    assert(!"timed out accepting TCP connection");
    return -1;
}

static void test_tcp(UnapiNet *net) {
    uint16_t port;
    int listener = loopback_socket(SOCK_STREAM, &port);
    int peer;
    u8 open_parameters[11] = {
        127, 0, 0, 1, (u8)port, (u8)(port >> 8),
        0xff, 0xff, 0, 0, 0
    };
    const u8 open_result[] = {1};
    const u8 handle[] = {1};
    u8 state[12];
    static const u8 send_parameters[] = {
        1, 5, 0, 'h', 'e', 'l', 'l', 'o'
    };
    static const u8 send_result[] = {0};
    char buffer[16];
    bool received_hello = false;
    bool received_world = false;

    assert(listen(listener, 1) == 0);
    write_parameters(net, open_parameters, sizeof(open_parameters));
    command(net, 0x03);
    expect_result(net, open_result, sizeof(open_result));
    peer = wait_for_accept(net, listener);

    for (unsigned i = 0; i < 2000; ++i) {
        write_parameters(net, handle, sizeof(handle));
        command(net, 0x07);
        assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
        for (unsigned j = 0; j < sizeof(state); ++j)
            state[j] = read_port(net, UNAPINET_DATA_PORT);
        if (state[0] == 4)
            break;
        SDL_Delay(1);
    }
    assert(state[0] == 4);

    write_parameters(net, send_parameters, sizeof(send_parameters));
    command(net, 0x04);
    expect_result(net, send_result, sizeof(send_result));
    for (unsigned i = 0; i < 2000; ++i) {
        int length;
        unapinet_poll(net);
        length = recv(peer, buffer, sizeof(buffer), 0);
        if (length > 0) {
            assert(length == 5);
            assert(memcmp(buffer, "hello", 5) == 0);
            received_hello = true;
            break;
        }
        assert(length < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));
        SDL_Delay(1);
    }
    assert(received_hello);
    assert(send(peer, "world", 5, 0) == 5);
    for (unsigned i = 0; i < 2000; ++i) {
        u8 recv_parameters[] = {1, 5, 0};
        u8 length_low;
        u8 length_high;

        unapinet_poll(net);
        write_parameters(net, recv_parameters, sizeof(recv_parameters));
        command(net, 0x05);
        assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
        length_low = read_port(net, UNAPINET_DATA_PORT);
        length_high = read_port(net, UNAPINET_DATA_PORT);
        if (length_low == 5 && length_high == 0) {
            for (unsigned j = 0; j < 5; ++j)
                buffer[j] = (char)read_port(net, UNAPINET_DATA_PORT);
            assert(memcmp(buffer, "world", 5) == 0);
            received_world = true;
            break;
        }
        assert(length_low == 0 && length_high == 0);
        SDL_Delay(1);
    }
    assert(received_world);
    write_parameters(net, handle, sizeof(handle));
    command(net, 0x08);
    expect_result(net, send_result, sizeof(send_result));
    close(peer);
    close(listener);
}

static void test_udp(UnapiNet *net) {
    uint16_t host_port;
    int host = loopback_socket(SOCK_DGRAM, &host_port);
    static const u8 udp_open[] = {0xff, 0xff};
    static const u8 handle_result[] = {1};
    u8 send_parameters[] = {
        1, 127, 0, 0, 1, (u8)host_port, (u8)(host_port >> 8),
        4, 0, 'p', 'i', 'n', 'g'
    };
    static const u8 okay[] = {0};
    struct sockaddr_in guest_address;
    socklen_t guest_length = sizeof(guest_address);
    char buffer[16];
    int received = -1;

    write_parameters(net, udp_open, sizeof(udp_open));
    command(net, 0x09);
    expect_result(net, handle_result, sizeof(handle_result));
    write_parameters(net, send_parameters, sizeof(send_parameters));
    command(net, 0x0c);
    expect_result(net, okay, sizeof(okay));
    for (unsigned i = 0; i < 2000; ++i) {
        received = recvfrom(host, buffer, sizeof(buffer), 0,
                            (struct sockaddr *)&guest_address,
                            &guest_length);
        if (received > 0)
            break;
        assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
        SDL_Delay(1);
    }
    assert(received == 4 && memcmp(buffer, "ping", 4) == 0);
    assert(sendto(host, "pong", 4, 0,
                  (struct sockaddr *)&guest_address,
                  guest_length) == 4);
    for (unsigned i = 0; i < 2000; ++i) {
        static const u8 state_parameters[] = {1};
        u8 low;
        u8 high;

        unapinet_poll(net);
        write_parameters(net, state_parameters, 1);
        command(net, 0x0b);
        assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
        low = read_port(net, UNAPINET_DATA_PORT);
        high = read_port(net, UNAPINET_DATA_PORT);
        if (low == 4 && high == 0)
            break;
        SDL_Delay(1);
    }
    {
        static const u8 recv_parameters[] = {1, 16, 0};
        u8 result[12];

        write_parameters(net, recv_parameters, sizeof(recv_parameters));
        command(net, 0x0f);
        assert(read_port(net, UNAPINET_COMMAND_PORT) == 2);
        for (unsigned i = 0; i < sizeof(result); ++i)
            result[i] = read_port(net, UNAPINET_DATA_PORT);
        assert(result[6] == 4 && result[7] == 0);
        assert(memcmp(result + 8, "pong", 4) == 0);
    }
    close(host);
}
#endif

int main(void) {
    UnapiNet *net = unapinet_create();
    u8 value = 0x5a;

    assert(net);
    test_wire_protocol(net);
    test_async_dns(net);
#ifndef _WIN32
    test_tcp(net);
    test_udp(net);
#endif
    assert(unapinet_take_activity(net));
    assert(!unapinet_take_activity(net));
    assert(unapinet_set_enabled(net, false));
    assert(!unapinet_io_read(net, UNAPINET_COMMAND_PORT, &value));
    assert(value == 0x5a);
    unapinet_destroy(net);
    puts("unapinet tests passed");
    return 0;
}
