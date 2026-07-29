#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ata.h"
#include "types.h"

#define MSX_SUNRISE_ROM_SIZE 0x20000u
#define MSX_SUNRISE_BANK_SIZE 0x4000u

typedef struct {
    u8 rom[MSX_SUNRISE_ROM_SIZE];
    AtaDevice master;
    u8 control;
    u8 bank;
    u8 read_latch;
    u8 write_latch;
    unsigned selected_device;
    bool registers_enabled;
    bool soft_reset;
    bool rom_loaded;
} MsxSunriseIde;

void sunrise_init(MsxSunriseIde *sunrise);
void sunrise_destroy(MsxSunriseIde *sunrise);
void sunrise_reset(MsxSunriseIde *sunrise);

int  sunrise_install_rom(MsxSunriseIde *sunrise,
                         const u8 *data, size_t size);
int  sunrise_eject_rom(MsxSunriseIde *sunrise);
int  sunrise_mount_disk(MsxSunriseIde *sunrise, const char *path);
int  sunrise_mount_disk_mode(MsxSunriseIde *sunrise, const char *path,
                             AtaImageMode mode);
int  sunrise_flush_disk(MsxSunriseIde *sunrise);
int  sunrise_eject_disk(MsxSunriseIde *sunrise);
bool sunrise_disk_mounted(const MsxSunriseIde *sunrise);
bool sunrise_disk_writable(const MsxSunriseIde *sunrise);
bool sunrise_disk_dirty(const MsxSunriseIde *sunrise);
bool sunrise_disk_has_error(const MsxSunriseIde *sunrise);
const char *sunrise_disk_error(const MsxSunriseIde *sunrise);
bool sunrise_take_activity(MsxSunriseIde *sunrise);

u8   sunrise_read(MsxSunriseIde *sunrise, u16 address);
void sunrise_write(MsxSunriseIde *sunrise, u16 address, u8 value);
