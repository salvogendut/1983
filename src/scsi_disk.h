#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "ata.h"
#include "types.h"

#define SCSI_DISK_SECTOR_SIZE 512u
#define SCSI_DISK_BUFFER_SIZE 512u

/* These values intentionally match the three SCSI phase signals as they
 * appear in the NCR/Z5380 target-command register (MSG, C/D, I/O). */
typedef enum {
    SCSI_PHASE_DATA_OUT    = 0,
    SCSI_PHASE_DATA_IN     = 1,
    SCSI_PHASE_COMMAND     = 2,
    SCSI_PHASE_STATUS      = 3,
    SCSI_PHASE_MESSAGE_OUT = 6,
    SCSI_PHASE_MESSAGE_IN  = 7,
    SCSI_PHASE_BUS_FREE    = 8
} ScsiPhase;

typedef struct {
    FILE *image;
    u64 sector_count;
    AtaImageMode image_mode;
    bool dirty;
    bool io_error;
    bool activity;
    char host_error[192];

    ScsiPhase phase;
    u8 cdb[16];
    size_t cdb_length;
    size_t cdb_expected;
    u8 buffer[SCSI_DISK_BUFFER_SIZE];
    size_t buffer_length;
    size_t buffer_offset;
    u64 transfer_lba;
    u32 blocks_remaining;
    size_t data_out_remaining;
    bool transfer_read;
    bool transfer_write;
    bool discard_data_out;
    u8 status;
    u8 message;
    u8 sense_key;
    u8 sense_asc;
    u8 sense_ascq;
} ScsiDisk;

void scsi_disk_init(ScsiDisk *disk);
void scsi_disk_destroy(ScsiDisk *disk);
void scsi_disk_bus_reset(ScsiDisk *disk);

int  scsi_disk_mount(ScsiDisk *disk, const char *path,
                     AtaImageMode mode);
int  scsi_disk_flush(ScsiDisk *disk);
int  scsi_disk_unmount(ScsiDisk *disk);
bool scsi_disk_mounted(const ScsiDisk *disk);
bool scsi_disk_writable(const ScsiDisk *disk);
bool scsi_disk_dirty(const ScsiDisk *disk);
bool scsi_disk_has_error(const ScsiDisk *disk);
const char *scsi_disk_error(const ScsiDisk *disk);
bool scsi_disk_take_activity(ScsiDisk *disk);

void scsi_disk_select(ScsiDisk *disk, bool attention);
ScsiPhase scsi_disk_phase(const ScsiDisk *disk);
bool scsi_disk_phase_is_input(ScsiPhase phase);
bool scsi_disk_phase_is_output(ScsiPhase phase);
u8   scsi_disk_current_byte(const ScsiDisk *disk);
void scsi_disk_accept_byte(ScsiDisk *disk, u8 value);
void scsi_disk_advance_byte(ScsiDisk *disk);
