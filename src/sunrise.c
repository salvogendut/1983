#include "sunrise.h"

#include <string.h>

static u8 reverse_byte(u8 value) {
    value = (u8)(((value & 0x55) << 1) | ((value >> 1) & 0x55));
    value = (u8)(((value & 0x33) << 2) | ((value >> 2) & 0x33));
    return (u8)((value << 4) | (value >> 4));
}

static void sunrise_write_control(MsxSunriseIde *sunrise, u8 value) {
    sunrise->control = value;
    sunrise->registers_enabled = (value & 1) != 0;
    sunrise->bank = reverse_byte(value & 0xf8) & 7;
}

static u16 sunrise_read_data(MsxSunriseIde *sunrise) {
    return sunrise->selected_device ? 0x7f7f
                                    : ata_read_data(&sunrise->master);
}

static void sunrise_write_data(MsxSunriseIde *sunrise, u16 value) {
    if (!sunrise->selected_device)
        ata_write_data(&sunrise->master, value);
}

static u8 sunrise_read_register(MsxSunriseIde *sunrise, unsigned reg) {
    u8 result;

    if (reg == 14)
        reg = 7;
    if (sunrise->soft_reset)
        return reg == 7 ? 0xff : 0x7f;
    if (reg == 0)
        return (u8)sunrise_read_data(sunrise);
    result = sunrise->selected_device
           ? 0x7f : ata_read_register(&sunrise->master, reg);
    if (reg == 6)
        result = (result & 0xef) |
                 (sunrise->selected_device ? 0x10 : 0);
    return result;
}

static void sunrise_write_register(MsxSunriseIde *sunrise,
                                   unsigned reg, u8 value) {
    if (sunrise->soft_reset) {
        if (reg == 14 && !(value & 0x04))
            sunrise->soft_reset = false;
        return;
    }
    if (reg == 0) {
        sunrise_write_data(sunrise, (u16)value | ((u16)value << 8));
        return;
    }
    if (reg == 14 && (value & 0x04)) {
        sunrise->soft_reset = true;
        ata_reset(&sunrise->master);
        return;
    }
    if (reg == 6)
        sunrise->selected_device = (value & 0x10) ? 1u : 0u;
    if (!sunrise->selected_device)
        ata_write_register(&sunrise->master, reg, value);
}

void sunrise_init(MsxSunriseIde *sunrise) {
    if (!sunrise)
        return;
    memset(sunrise, 0, sizeof(*sunrise));
    memset(sunrise->rom, 0xff, sizeof(sunrise->rom));
    ata_init(&sunrise->master);
    sunrise_write_control(sunrise, 0xff);
}

void sunrise_destroy(MsxSunriseIde *sunrise) {
    if (!sunrise)
        return;
    ata_destroy(&sunrise->master);
    memset(sunrise, 0, sizeof(*sunrise));
}

void sunrise_reset(MsxSunriseIde *sunrise) {
    if (!sunrise)
        return;
    sunrise->selected_device = 0;
    sunrise->soft_reset = false;
    ata_reset(&sunrise->master);
}

int sunrise_install_rom(MsxSunriseIde *sunrise,
                        const u8 *data, size_t size) {
    if (!sunrise || !data || size != MSX_SUNRISE_ROM_SIZE)
        return -1;
    memcpy(sunrise->rom, data, size);
    sunrise->rom_loaded = true;
    sunrise_write_control(sunrise, 0xff);
    sunrise_reset(sunrise);
    return 0;
}

int sunrise_eject_rom(MsxSunriseIde *sunrise) {
    if (!sunrise)
        return -1;
    if (sunrise_eject_disk(sunrise) != 0)
        return -1;
    memset(sunrise->rom, 0xff, sizeof(sunrise->rom));
    sunrise->rom_loaded = false;
    sunrise_write_control(sunrise, 0xff);
    sunrise_reset(sunrise);
    return 0;
}

int sunrise_mount_disk(MsxSunriseIde *sunrise, const char *path) {
    return sunrise_mount_disk_mode(
        sunrise, path, ATA_IMAGE_READ_ONLY);
}

int sunrise_mount_disk_mode(MsxSunriseIde *sunrise, const char *path,
                            AtaImageMode mode) {
    if (!sunrise || !sunrise->rom_loaded)
        return -1;
    return ata_mount_mode(&sunrise->master, path, mode);
}

int sunrise_flush_disk(MsxSunriseIde *sunrise) {
    return sunrise ? ata_flush(&sunrise->master) : -1;
}

int sunrise_eject_disk(MsxSunriseIde *sunrise) {
    return sunrise ? ata_unmount(&sunrise->master) : -1;
}

bool sunrise_disk_mounted(const MsxSunriseIde *sunrise) {
    return sunrise && ata_is_mounted(&sunrise->master);
}

bool sunrise_disk_writable(const MsxSunriseIde *sunrise) {
    return sunrise && ata_is_writable(&sunrise->master);
}

bool sunrise_disk_dirty(const MsxSunriseIde *sunrise) {
    return sunrise && ata_is_dirty(&sunrise->master);
}

bool sunrise_disk_has_error(const MsxSunriseIde *sunrise) {
    return sunrise && ata_has_io_error(&sunrise->master);
}

const char *sunrise_disk_error(const MsxSunriseIde *sunrise) {
    return sunrise ? ata_last_error(&sunrise->master) : "";
}

bool sunrise_take_activity(MsxSunriseIde *sunrise) {
    return sunrise && ata_take_activity(&sunrise->master);
}

u8 sunrise_read(MsxSunriseIde *sunrise, u16 address) {
    if (!sunrise || !sunrise->rom_loaded)
        return 0xff;
    if (sunrise->registers_enabled &&
        (address & 0x3e00) == 0x3c00) {
        if (!(address & 1)) {
            u16 value = sunrise_read_data(sunrise);

            sunrise->read_latch = (u8)(value >> 8);
            return (u8)value;
        }
        return sunrise->read_latch;
    }
    if (sunrise->registers_enabled &&
        (address & 0x3f00) == 0x3e00)
        return sunrise_read_register(sunrise, address & 0x0f);
    if (address >= 0x4000 && address < 0x8000)
        return sunrise->rom[
            (size_t)sunrise->bank * MSX_SUNRISE_BANK_SIZE +
            (address & 0x3fff)];
    return 0xff;
}

void sunrise_write(MsxSunriseIde *sunrise, u16 address, u8 value) {
    if (!sunrise || !sunrise->rom_loaded)
        return;
    if ((address & 0xbf04) == 0x0104) {
        sunrise_write_control(sunrise, value);
        return;
    }
    if (sunrise->registers_enabled &&
        (address & 0x3e00) == 0x3c00) {
        if (!(address & 1)) {
            sunrise->write_latch = value;
        } else {
            sunrise_write_data(
                sunrise,
                sunrise->write_latch | ((u16)value << 8));
        }
        return;
    }
    if (sunrise->registers_enabled &&
        (address & 0x3f00) == 0x3e00)
        sunrise_write_register(sunrise, address & 0x0f, value);
}
