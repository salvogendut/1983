#pragma once

#include <stddef.h>

#include "types.h"

/* Browser transport for the openMSXnet-compatible UNAPI port device. The
 * guest protocol remains in WASM; JavaScript owns only the WebSocket relay and
 * its bounded asynchronous socket queues. */
void unapinet_web_set_device(int enabled);
void unapinet_web_reset_channels(void);
int  unapinet_web_take_activity(void);

int unapinet_web_dns(const char *host);

int  unapinet_web_tcp_open(int slot, const char *host, u16 port, u8 flags);
int  unapinet_web_tcp_send(int slot, const u8 *data, size_t length);
void unapinet_web_tcp_close(int slot);
int  unapinet_web_tcp_read(int slot, u8 *data, size_t maximum);
int  unapinet_web_tcp_available(int slot);

int  unapinet_web_udp_open(int slot, u16 local_port);
int  unapinet_web_udp_send(int slot, const u8 address[4], u16 port,
                           const u8 *data, size_t length);
void unapinet_web_udp_close(int slot);
int  unapinet_web_udp_read(int slot, u8 address[4], u16 *port,
                           u8 *data, size_t maximum);
int  unapinet_web_udp_available(int slot);
