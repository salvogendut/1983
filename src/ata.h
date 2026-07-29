#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "types.h"

#define ATA_SECTOR_SIZE 512u

#define ATA_STATUS_BSY  0x80u
#define ATA_STATUS_DRDY 0x40u
#define ATA_STATUS_DSC  0x10u
#define ATA_STATUS_DRQ  0x08u
#define ATA_STATUS_ERR  0x01u

#define ATA_ERROR_UNC  0x40u
#define ATA_ERROR_IDNF 0x10u
#define ATA_ERROR_ABRT 0x04u

typedef struct {
    u8 error;
    u8 features;
    u8 sector_count;
    u8 lba_low;
    u8 lba_mid;
    u8 lba_high;
    u8 device;
    u8 status;
    u8 command;

    u8 sector[ATA_SECTOR_SIZE];
    size_t transfer_offset;
    u32 transfer_lba;
    unsigned sectors_left;
    bool transfer_read;
    bool activity;

    FILE *image;
    u64 sector_count_total;
} AtaDevice;

void ata_init(AtaDevice *ata);
void ata_destroy(AtaDevice *ata);
void ata_reset(AtaDevice *ata);

int  ata_mount(AtaDevice *ata, const char *path);
void ata_unmount(AtaDevice *ata);
bool ata_is_mounted(const AtaDevice *ata);
u64  ata_total_sectors(const AtaDevice *ata);
bool ata_take_activity(AtaDevice *ata);

u16  ata_read_data(AtaDevice *ata);
void ata_write_data(AtaDevice *ata, u16 value);
u8   ata_read_register(const AtaDevice *ata, unsigned reg);
void ata_write_register(AtaDevice *ata, unsigned reg, u8 value);
