#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "floppy.h"
#include "types.h"

#define WD2793_STATUS_NOT_READY     0x80u
#define WD2793_STATUS_WRITE_PROTECT 0x40u
#define WD2793_STATUS_RECORD_TYPE   0x20u
#define WD2793_STATUS_RECORD_MISSING 0x10u
#define WD2793_STATUS_CRC_ERROR     0x08u
#define WD2793_STATUS_TRACK_ZERO    0x04u
#define WD2793_STATUS_LOST_DATA     0x04u
#define WD2793_STATUS_INDEX         0x02u
#define WD2793_STATUS_DRQ           0x02u
#define WD2793_STATUS_BUSY          0x01u

/* A 300 rpm drive completes one revolution in 200 ms. These values express
 * that rotation and a roughly 4 ms index pulse in 3.579545 MHz MSX cycles. */
#define WD2793_INDEX_PERIOD_CYCLES 715909u
#define WD2793_INDEX_PULSE_CYCLES   14318u

typedef enum {
    WD2793_TRANSFER_NONE = 0,
    WD2793_TRANSFER_READ,
    WD2793_TRANSFER_WRITE
} Wd2793Transfer;

typedef struct {
    FloppyImage drive_a;
    FloppyImage drive_b;

    u8 command;
    u8 track;
    u8 sector;
    u8 data;
    u8 side_reg;
    u8 drive_reg;
    u8 physical_track;
    int step_direction;
    int selected_drive;

    u8 status_error;
    bool last_type_one;
    bool busy;
    bool drq;
    bool irq;
    bool multiple;
    bool motor;
    u32 index_cycles;

    Wd2793Transfer transfer;
    u8 transfer_data[FLOPPY_SECTOR_SIZE];
    size_t transfer_size;
    size_t transfer_offset;
} Wd2793;

void wd2793_init(Wd2793 *fdc);
void wd2793_destroy(Wd2793 *fdc);
void wd2793_reset(Wd2793 *fdc);
void wd2793_advance(Wd2793 *fdc, unsigned cycles);

bool wd2793_handles_address(u16 address);
u8 wd2793_read_memory(Wd2793 *fdc, u16 address);
void wd2793_write_memory(Wd2793 *fdc, u16 address, u8 value);

/* Port-mapped cartridge FDC (CDX-2 / disk type 5, ports D0-D3). */
u8 wd2793_read_port(Wd2793 *fdc, u8 reg);
void wd2793_write_port(Wd2793 *fdc, u8 reg, u8 value);

int wd2793_mount_drive_a(Wd2793 *fdc, const char *path,
                         FloppyImageMode mode);
int wd2793_flush_drive_a(Wd2793 *fdc);
int wd2793_eject_drive_a(Wd2793 *fdc);
bool wd2793_drive_a_mounted(const Wd2793 *fdc);
bool wd2793_drive_a_writable(const Wd2793 *fdc);
bool wd2793_drive_a_dirty(const Wd2793 *fdc);
bool wd2793_drive_a_has_error(const Wd2793 *fdc);
const char *wd2793_drive_a_error(const Wd2793 *fdc);
bool wd2793_take_drive_a_activity(Wd2793 *fdc);
int wd2793_mount_drive_b(Wd2793 *fdc, const char *path,
                         FloppyImageMode mode);
int wd2793_flush_drive_b(Wd2793 *fdc);
int wd2793_eject_drive_b(Wd2793 *fdc);
bool wd2793_drive_b_mounted(const Wd2793 *fdc);
bool wd2793_drive_b_writable(const Wd2793 *fdc);
bool wd2793_drive_b_dirty(const Wd2793 *fdc);
bool wd2793_drive_b_has_error(const Wd2793 *fdc);
const char *wd2793_drive_b_error(const Wd2793 *fdc);
bool wd2793_take_drive_b_activity(Wd2793 *fdc);
