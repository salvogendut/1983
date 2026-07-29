#include "sunrise.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *fixture_path = "test-sunrise-image.tmp";

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");

    assert(file);
    for (unsigned i = 0; i < ATA_SECTOR_SIZE; ++i)
        assert(fputc((int)(i ^ 0x83), file) != EOF);
    assert(fclose(file) == 0);
}

int main(void) {
    MsxSunriseIde sunrise;
    u8 rom[MSX_SUNRISE_ROM_SIZE];

    for (unsigned bank = 0; bank < 8; ++bank)
        memset(&rom[bank * MSX_SUNRISE_BANK_SIZE],
               (int)bank, MSX_SUNRISE_BANK_SIZE);
    create_fixture();

    sunrise_init(&sunrise);
    assert(sunrise_read(&sunrise, 0x4000) == 0xff);
    assert(sunrise_install_rom(&sunrise, rom, sizeof(rom)) == 0);
    assert(sunrise.bank == 7);
    assert(sunrise.registers_enabled);
    assert(sunrise_read(&sunrise, 0x4000) == 7);

    /* Control bits 7..3 are bit-reversed into the ROM bank number. */
    sunrise_write(&sunrise, 0x4104, 0x81);
    assert(sunrise.bank == 1);
    assert(sunrise_read(&sunrise, 0x4000) == 1);
    sunrise_write(&sunrise, 0x0104, 0x41);
    assert(sunrise.bank == 2);
    assert(sunrise_read(&sunrise, 0x4000) == 2);

    /* With register overlays disabled, the address reads banked ROM. */
    sunrise_write(&sunrise, 0x4104, 0x40);
    assert(!sunrise.registers_enabled);
    assert(sunrise_read(&sunrise, 0x7e07) == 2);

    assert(sunrise_mount_disk(&sunrise, fixture_path) == 0);
    assert(sunrise_disk_mounted(&sunrise));
    sunrise_write(&sunrise, 0x4104, 0x01);
    assert(sunrise.bank == 0);
    assert(sunrise.registers_enabled);
    sunrise_write(&sunrise, 0x7e02, 1);
    sunrise_write(&sunrise, 0x7e03, 0);
    sunrise_write(&sunrise, 0x7e04, 0);
    sunrise_write(&sunrise, 0x7e05, 0);
    sunrise_write(&sunrise, 0x7e06, 0xe0);
    sunrise_write(&sunrise, 0x7e07, 0x20);
    assert(sunrise_take_activity(&sunrise));
    for (unsigned i = 0; i < ATA_SECTOR_SIZE; i += 2) {
        u8 low = sunrise_read(&sunrise, 0x7c00);
        u8 high = sunrise_read(&sunrise, 0x7c01);

        assert(low == (u8)(i ^ 0x83));
        assert(high == (u8)((i + 1) ^ 0x83));
        assert(sunrise_read(&sunrise, 0x7c01) == high);
    }
    assert(sunrise_read(&sunrise, 0x7e07) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));

    /* The slave is an unconnected device and returns open-bus values. */
    sunrise_write(&sunrise, 0x7e06, 0x10);
    assert(sunrise.selected_device == 1);
    assert(sunrise_read(&sunrise, 0x7e07) == 0x7f);
    sunrise_write(&sunrise, 0x7e06, 0xe0);
    assert(sunrise.selected_device == 0);

    sunrise_write(&sunrise, 0x7e0e, 0x04);
    assert(sunrise.soft_reset);
    assert(sunrise_read(&sunrise, 0x7e07) == 0xff);
    assert(sunrise_read(&sunrise, 0x7e03) == 0x7f);
    sunrise_write(&sunrise, 0x7e0e, 0x00);
    assert(!sunrise.soft_reset);
    assert(sunrise_read(&sunrise, 0x7e07) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));

    sunrise_reset(&sunrise);
    assert(sunrise.bank == 0);
    assert(sunrise.registers_enabled);
    assert(sunrise_disk_mounted(&sunrise));

    sunrise_eject_rom(&sunrise);
    assert(!sunrise.rom_loaded);
    assert(!sunrise_disk_mounted(&sunrise));
    sunrise_destroy(&sunrise);
    assert(remove(fixture_path) == 0);
    return 0;
}
