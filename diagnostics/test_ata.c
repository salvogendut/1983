#define _POSIX_C_SOURCE 200112L

#include "ata.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define TEST_CLOSE _close
#define TEST_FILENO _fileno
#else
#include <unistd.h>
#define TEST_CLOSE close
#define TEST_FILENO fileno
#endif

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

static u8 written_byte(size_t offset) {
    return (u8)(0xa5u ^ (offset * 13u));
}

static void write_sector_data(AtaDevice *ata, size_t first,
                              size_t bytes) {
    assert((first & 1u) == 0);
    assert((bytes & 1u) == 0);
    for (size_t i = first; i < first + bytes; i += 2)
        ata_write_data(
            ata, written_byte(i) |
                 ((u16)written_byte(i + 1) << 8));
}

static void assert_file_sector(size_t lba, bool written) {
    FILE *file = fopen(fixture_path, "rb");
    u8 data[ATA_SECTOR_SIZE];

    assert(file);
    assert(fseek(file, (long)(lba * ATA_SECTOR_SIZE), SEEK_SET) == 0);
    assert(fread(data, 1, sizeof(data), file) == sizeof(data));
    assert(fclose(file) == 0);
    for (size_t i = 0; i < sizeof(data); ++i)
        assert(data[i] ==
               (written ? written_byte(i) :
                fixture_byte(lba * ATA_SECTOR_SIZE + i)));
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
    assert(identify[82] & 0x1000);
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));
}

static void test_symbos_native_max(AtaDevice *ata) {
    /*
     * SymbOS uses READ NATIVE MAX ADDRESS as a capacity query. Match the
     * Sunrise IDE behaviour in openMSX: return the sector count, not the
     * zero-based last LBA, so a partition ending at the final sector is not
     * rejected as extending beyond the device.
     */
    select_lba(ata, 0, 1);
    ata_write_register(ata, 7, 0xf8);
    assert(ata_read_register(ata, 3) == 2);
    assert(ata_read_register(ata, 4) == 0);
    assert(ata_read_register(ata, 5) == 0);
    assert((ata_read_register(ata, 6) & 0x0f) == 0);
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

static void test_read_only_safety(AtaDevice *ata) {
    select_lba(ata, 0, 1);
    ata_write_register(ata, 7, 0x30);
    assert(ata_read_register(ata, 1) == ATA_ERROR_ABRT);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE);
    assert(!ata_is_dirty(ata));
    assert(ata_flush(ata) == 0);
    assert_file_sector(0, false);
}

static void test_writes_and_flush(AtaDevice *ata) {
    assert(ata_mount_mode(
               ata, fixture_path, ATA_IMAGE_READ_WRITE) == 0);
    assert(ata_is_writable(ata));

    select_lba(ata, 0, 1);
    ata_write_register(ata, 7, 0x30);
    assert(ata_read_register(ata, 7) & ATA_STATUS_DRQ);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE);
    assert(ata_read_register(ata, 7) ==
           (ATA_STATUS_DRDY | ATA_STATUS_DSC));
    assert(ata_is_dirty(ata));
    assert(ata_take_activity(ata));
    assert(ata_flush(ata) == 0);
    assert(!ata_is_dirty(ata));
    assert_file_sector(0, true);

    /* FLUSH CACHE and FLUSH CACHE EXT are both accepted. */
    select_lba(ata, 1, 1);
    ata_write_register(ata, 7, 0x31);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE);
    assert(ata_is_dirty(ata));
    ata_write_register(ata, 7, 0xe7);
    assert(!ata_is_dirty(ata));
    assert(!(ata_read_register(ata, 7) & ATA_STATUS_ERR));
    assert_file_sector(1, true);

    /* A failed replacement keeps the current image and its mode. */
    assert(ata_mount_mode(
               ata, "missing-ata-image.tmp",
               ATA_IMAGE_READ_ONLY) == -1);
    assert(ata_is_mounted(ata));
    assert(ata_is_writable(ata));
    assert(ata_last_error(ata)[0]);
}

static void test_partial_write_reset_is_non_destructive(
    AtaDevice *ata) {
    create_fixture();
    assert(ata_mount_mode(
               ata, fixture_path, ATA_IMAGE_READ_WRITE) == 0);
    select_lba(ata, 1, 1);
    ata_write_register(ata, 7, 0x30);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE / 2);
    assert(!ata_is_dirty(ata));
    ata_reset(ata);
    assert(ata_is_mounted(ata));
    assert(ata_is_writable(ata));
    assert(!ata_is_dirty(ata));
    assert(ata_unmount(ata) == 0);
    assert_file_sector(0, false);
    assert_file_sector(1, false);
}

static void test_multi_sector_bounds_and_safe_eject(AtaDevice *ata) {
    create_fixture();
    assert(ata_mount_mode(
               ata, fixture_path, ATA_IMAGE_READ_WRITE) == 0);
    select_lba(ata, 0, 2);
    ata_write_register(ata, 7, 0x30);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE);
    assert(ata_is_dirty(ata));
    ata_reset(ata);
    assert(ata_is_dirty(ata));
    assert(ata_is_writable(ata));
    assert(ata_unmount(ata) == 0);
    assert(!ata_is_mounted(ata));
    assert_file_sector(0, true);
    assert_file_sector(1, true);

    create_fixture();
    assert(ata_mount_mode(
               ata, fixture_path, ATA_IMAGE_READ_WRITE) == 0);
    select_lba(ata, 1, 2);
    ata_write_register(ata, 7, 0x30);
    assert(ata_read_register(ata, 1) == ATA_ERROR_IDNF);
    write_sector_data(ata, 0, ATA_SECTOR_SIZE);
    assert(!ata_is_dirty(ata));
    assert(ata_unmount(ata) == 0);
    assert_file_sector(0, false);
    assert_file_sector(1, false);
}

static void test_flush_failure_blocks_ejection(void) {
    AtaDevice ata;
    int descriptor;

    create_fixture();
    ata_init(&ata);
    assert(ata_mount_mode(
               &ata, fixture_path, ATA_IMAGE_READ_WRITE) == 0);
    select_lba(&ata, 0, 1);
    ata_write_register(&ata, 7, 0x30);
    write_sector_data(&ata, 0, ATA_SECTOR_SIZE);
    assert(ata_is_dirty(&ata));

    /*
     * Simulate a host device disappearing after accepting buffered data.
     * The image must remain attached and visibly dirty when flush fails.
     */
    descriptor = TEST_FILENO(ata.image);
    assert(descriptor >= 0);
    assert(TEST_CLOSE(descriptor) == 0);
    ata_write_register(&ata, 7, 0xe7);
    assert(ata_read_register(&ata, 1) == ATA_ERROR_UNC);
    assert(ata_read_register(&ata, 7) & ATA_STATUS_ERR);
    assert(ata_unmount(&ata) == -1);
    assert(ata_is_mounted(&ata));
    assert(ata_is_dirty(&ata));
    assert(ata_has_io_error(&ata));
    assert(strstr(ata_last_error(&ata), "flush"));
    assert_file_sector(0, false);
    assert_file_sector(1, false);
    ata_destroy(&ata);
    assert_file_sector(0, false);
    assert_file_sector(1, false);
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
    test_symbos_native_max(&ata);
    test_sector_reads(&ata);
    test_errors_and_reset(&ata);
    test_read_only_safety(&ata);
    test_writes_and_flush(&ata);
    assert(ata_unmount(&ata) == 0);
    test_partial_write_reset_is_non_destructive(&ata);
    test_multi_sector_bounds_and_safe_eject(&ata);

    assert(ata_unmount(&ata) == 0);
    assert(!ata_is_mounted(&ata));
    assert(ata_read_register(&ata, 7) == 0x7f);
    ata_destroy(&ata);
    test_flush_failure_blocks_ejection();
    assert(remove(fixture_path) == 0);
    return 0;
}
