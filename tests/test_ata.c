#include "ata.h"

#include <assert.h>
#include <stdio.h>

static const char *fixture_path = "test-ata-image.tmp";

static u8 fixture_byte(size_t offset) {
    return (u8)(offset * 7u + 3u);
}

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");

    assert(file);
    for (size_t i = 0; i < ATA_SECTOR_SIZE * 2; ++i)
        assert(fputc(fixture_byte(i), file) != EOF);
    assert(fclose(file) == 0);
}

static void select_lba(AtaDevice *ata, u32 lba, u8 count) {
    ata_write_register(ata, 2, count);
    ata_write_register(ata, 3, (u8)lba);
    ata_write_register(ata, 4, (u8)(lba >> 8));
    ata_write_register(ata, 5, (u8)(lba >> 16));
    ata_write_register(ata, 6, 0xe0 | (u8)(lba >> 24));
}

static void test_identify(AtaDevice *ata) {
    u16 identify[256];

    ata_write_register(ata, 7, 0xec);
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_DRQ));
    for (unsigned i = 0; i < 256; ++i)
        identify[i] = ata_read_data(ata);
    assert(identify[0] == 0x0040);
    assert(identify[49] & 0x0200);
    assert(identify[60] == 2);
    assert(identify[61] == 0);
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));
}

static void test_sector_reads(AtaDevice *ata) {
    select_lba(ata, 1, 1);
    ata_write_register(ata, 7, 0x20);
    assert(ata_take_activity(ata));
    assert(!ata_take_activity(ata));
    for (size_t i = ATA_SECTOR_SIZE;
         i < ATA_SECTOR_SIZE * 2; i += 2) {
        u16 expected =
            fixture_byte(i) | ((u16)fixture_byte(i + 1) << 8);

        assert(ata_read_data(ata) == expected);
    }
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));

    select_lba(ata, 0, 2);
    ata_write_register(ata, 7, 0x21);
    for (size_t i = 0; i < ATA_SECTOR_SIZE * 2; i += 2) {
        u16 expected =
            fixture_byte(i) | ((u16)fixture_byte(i + 1) << 8);

        assert(ata_read_data(ata) == expected);
    }
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));
    assert(ata_take_activity(ata));
}

static void test_errors_and_reset(AtaDevice *ata) {
    select_lba(ata, 2, 1);
    ata_write_register(ata, 7, 0x20);
    assert(ata_read_register(ata, 1) == ATA_ERROR_IDNF);
    assert(ata_read_register(ata, 7) & ATA_STATUS_ERR);

    select_lba(ata, 0, 1);
    ata_write_register(ata, 7, 0x30);
    assert(ata_read_register(ata, 1) == ATA_ERROR_ABRT);

    select_lba(ata, 0, 1);
    ata_write_register(ata, 7, 0x20);
    assert(ata_read_register(ata, 7) & ATA_STATUS_DRQ);
    ata_reset(ata);
    assert(ata_is_mounted(ata));
    assert(ata_total_sectors(ata) == 2);
    assert(ata_read_register(ata, 2) == 1);
    assert(ata_read_register(ata, 3) == 1);
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));
}

int main(void) {
    AtaDevice ata;

    create_fixture();
    ata_init(&ata);
    assert(!ata_is_mounted(&ata));
    assert(ata_read_register(&ata, 7) == 0x7f);
    assert(ata_read_data(&ata) == 0x7f7f);
    assert(ata_mount(&ata, fixture_path) == 0);
    assert(ata_is_mounted(&ata));
    assert(ata_total_sectors(&ata) == 2);
    assert(ata_mount(&ata, "missing-ata-image.tmp") == -1);
    assert(ata_is_mounted(&ata));

    test_identify(&ata);
    test_sector_reads(&ata);
    test_errors_and_reset(&ata);

    ata_unmount(&ata);
    assert(!ata_is_mounted(&ata));
    assert(ata_read_register(&ata, 7) == 0x7f);
    ata_destroy(&ata);
    assert(remove(fixture_path) == 0);
    return 0;
}
