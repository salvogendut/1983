#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ata.h"
#include "scsi_disk.h"
#include "types.h"

#define MSX_SCSI_ROM_BANK_SIZE 0x4000u
#define MSX_SCSI_ROM_MAX_SIZE  0x80000u
#define MSX_SCSI_DEFAULT_TARGET_ID 0u

typedef enum {
    MSX_SCSI_DMA_NONE = 0,
    MSX_SCSI_DMA_SEND,
    MSX_SCSI_DMA_RECEIVE
} MsxScsiDmaDirection;

typedef struct {
    u8 *rom;
    size_t rom_size;
    u8 rom_bank;
    bool rom_loaded;

    ScsiDisk disk;
    unsigned target_id;

    u8 output_data;
    u8 initiator_command;
    u8 mode;
    u8 target_command;
    u8 select_enable;
    u8 input_data;
    u8 status_latch;
    ScsiPhase bus_phase;
    MsxScsiDmaDirection dma_direction;
    bool target_selected;
    bool selection_wait;
    bool request;
    bool acknowledge;
    bool dma_request;
    bool interrupt_request;
    bool lost_arbitration;
    bool arbitration_in_progress;
    bool test_mode;
    bool pending_input_advance;
    unsigned trace_rom_reads;
    unsigned trace_bank_writes;
    unsigned trace_io_events;
} MsxScsi;

void msx_scsi_init(MsxScsi *scsi);
void msx_scsi_destroy(MsxScsi *scsi);
void msx_scsi_reset(MsxScsi *scsi);

int  msx_scsi_install_rom(MsxScsi *scsi, const u8 *data, size_t size);
int  msx_scsi_eject_rom(MsxScsi *scsi);
bool msx_scsi_rom_loaded(const MsxScsi *scsi);

void msx_scsi_set_target_id(MsxScsi *scsi, unsigned target_id);
unsigned msx_scsi_target_id(const MsxScsi *scsi);

int  msx_scsi_mount_disk(MsxScsi *scsi, const char *path,
                         AtaImageMode mode);
int  msx_scsi_flush_disk(MsxScsi *scsi);
int  msx_scsi_eject_disk(MsxScsi *scsi);
bool msx_scsi_disk_mounted(const MsxScsi *scsi);
bool msx_scsi_disk_writable(const MsxScsi *scsi);
bool msx_scsi_disk_dirty(const MsxScsi *scsi);
bool msx_scsi_disk_has_error(const MsxScsi *scsi);
const char *msx_scsi_disk_error(const MsxScsi *scsi);
bool msx_scsi_take_activity(MsxScsi *scsi);

u8   msx_scsi_memory_read(MsxScsi *scsi, u16 address);
void msx_scsi_memory_write(MsxScsi *scsi, u16 address, u8 value);
bool msx_scsi_io_read(MsxScsi *scsi, u16 port, u8 *value);
bool msx_scsi_io_write(MsxScsi *scsi, u16 port, u8 value);
