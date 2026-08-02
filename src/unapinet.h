#pragma once

#include <stdbool.h>

#include "types.h"

#define UNAPINET_COMMAND_PORT 0x28u
#define UNAPINET_DATA_PORT 0x29u

typedef struct UnapiNet UnapiNet;

/*
 * Host-side half of the openMSXnet TCP/IP UNAPI bridge. The guest-side
 * UNAPINET.COM TSR discovers this device through a 0xAB handshake and
 * marshals standard TCP/IP UNAPI 1.1 calls over ports 0x28/0x29.
 */
UnapiNet *unapinet_create(void);
void unapinet_destroy(UnapiNet *net);

bool unapinet_set_enabled(UnapiNet *net, bool enabled);
bool unapinet_enabled(const UnapiNet *net);
bool unapinet_guest_driver_active(const UnapiNet *net);
void unapinet_reset(UnapiNet *net);
void unapinet_poll(UnapiNet *net);

/* MsxMachine optional-I/O-device callbacks. */
bool unapinet_io_read(void *context, u16 port, u8 *value);
bool unapinet_io_write(void *context, u16 port, u8 value);
void unapinet_io_reset(void *context);

bool unapinet_take_activity(UnapiNet *net);
const char *unapinet_error(const UnapiNet *net);
