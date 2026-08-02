/* rs232_dev.h - MSX RS-232C interface device: hosts the Intel 8251 USART,
 * the 8254 timer and (on Linux/BSD) the PTY/TCP host backend, and maps the
 * MSX I/O window 80h-87h. Ported in spirit from openMSX MSXRS232 (GPL-2.0+);
 * the 8251/8254 register models live in serial_8251.{h,c} and serial_8254.{h,c}.
 */
#pragma once

#include "types.h"

#include <stdbool.h>

typedef struct Rs232Device Rs232Device;

Rs232Device *rs232dev_create(void);
void rs232dev_destroy(Rs232Device *dev);

bool rs232dev_set_enabled(Rs232Device *dev, bool enabled);
bool rs232dev_enabled(const Rs232Device *dev);

/* MsxMachine optional-I/O-device callbacks. */
bool rs232dev_io_read(void *context, u16 port, u8 *value);
bool rs232dev_io_write(void *context, u16 port, u8 value);
void rs232dev_io_reset(void *context);
void rs232dev_io_advance(void *context, unsigned cycles);

/* Activity for the split RX(green)/TX(red) status LED. */
bool rs232dev_take_rx_activity(Rs232Device *dev);
bool rs232dev_take_tx_activity(Rs232Device *dev);

/* Host-side device name for the overlay (e.g. /tmp/1983-rs232 or the pty
 * node). Empty string when the host backend is unavailable. */
const char *rs232dev_host_device(const Rs232Device *dev);