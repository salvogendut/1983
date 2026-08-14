#define _POSIX_C_SOURCE 200112L

#include "floppy.h"

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

static const char *fixture_path = "test-floppy-image.tmp";
static const char *standard_dsk_path = "test-standard-cpc.dsk";
static const char *extended_dsk_path = "test-extended-cpc.dsk";

static u8 sector_byte(unsigned logical_sector, size_t offset) {
    return (u8)(logical_sector * 17u + offset * 7u + 3u);
}

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");
    u8 sector[FLOPPY_SECTOR_SIZE];

    assert(file);
    for (unsigned logical = 0; logical < 1440; ++logical) {
        for (size_t i = 0; i < sizeof(sector); ++i)
            sector[i] = sector_byte(logical, i);
        if (logical == 0) {
            sector[11] = 0x00;
            sector[12] = 0x02;
            sector[19] = 0xa0;
            sector[20] = 0x05;
            sector[24] = 9;
            sector[25] = 0;
            sector[26] = 2;
            sector[27] = 0;
        }
        assert(fwrite(sector, 1, sizeof(sector), file) ==
               sizeof(sector));
    }
    assert(fclose(file) == 0);
}

static void assert_sector_pattern(const u8 *sector,
                                  unsigned logical_sector) {
    for (size_t i = 0; i < FLOPPY_SECTOR_SIZE; ++i) {
        if (logical_sector == 0 &&
            ((i >= 11 && i <= 12) ||
             (i >= 19 && i <= 20) ||
             (i >= 24 && i <= 27)))
            continue;
        assert(sector[i] == sector_byte(logical_sector, i));
    }
}

static void create_cpc_fixture(const char *path, bool extended,
                               unsigned first_sector_id) {
    FILE *file = fopen(path, "wb");
    u8 disk_header[256] = { 0 };
    u8 track_header[256];
    u8 sector[FLOPPY_SECTOR_SIZE];
    const unsigned track_size = 256 + 9 * FLOPPY_SECTOR_SIZE;

    assert(file);
    if (extended) {
        memcpy(disk_header,
               "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
        disk_header[0x34] = track_size / 256;
        disk_header[0x35] = track_size / 256;
    } else {
        memcpy(disk_header,
               "MV - CPCEMU Disk-File\r\nDisk-Info\r\n", 34);
        disk_header[0x32] = track_size & 0xff;
        disk_header[0x33] = track_size >> 8;
    }
    disk_header[0x30] = 2;
    disk_header[0x31] = 1;
    assert(fwrite(disk_header, 1, sizeof(disk_header), file) ==
           sizeof(disk_header));

    for (unsigned track = 0; track < 2; ++track) {
        memset(track_header, 0, sizeof(track_header));
        memcpy(track_header, "Track-Info\r\n", 12);
        track_header[0x10] = (u8)track;
        track_header[0x11] = 0;
        track_header[0x14] = 2;
        track_header[0x15] = 9;
        for (unsigned index = 0; index < 9; ++index) {
            u8 *descriptor = track_header + 0x18 + index * 8;

            descriptor[0] = (u8)track;
            descriptor[1] = 0;
            descriptor[2] = (u8)(first_sector_id + index);
            descriptor[3] = 2;
            if (extended) {
                descriptor[6] = FLOPPY_SECTOR_SIZE & 0xff;
                descriptor[7] = FLOPPY_SECTOR_SIZE >> 8;
            }
        }
        assert(fwrite(track_header, 1, sizeof(track_header), file) ==
               sizeof(track_header));
        for (unsigned index = 0; index < 9; ++index) {
            memset(sector, (int)(0x20 + track * 9 + index),
                   sizeof(sector));
            assert(fwrite(sector, 1, sizeof(sector), file) ==
                   sizeof(sector));
        }
    }
    assert(fclose(file) == 0);
}

static void test_cpc_dsk_container(FloppyImage *image, const char *path,
                                   bool extended,
                                   unsigned first_sector_id) {
    u8 sector[FLOPPY_SECTOR_SIZE];
    u8 replacement[FLOPPY_SECTOR_SIZE];
    unsigned id;

    create_cpc_fixture(path, extended, first_sector_id);
    assert(floppy_image_mount(
               image, path, FLOPPY_IMAGE_READ_WRITE) == 0);
    assert(image->format == FLOPPY_FORMAT_CPC_DSK);
    assert(image->tracks == 2);
    assert(image->sides == 1);
    assert(image->sectors_per_track == 9);
    assert(floppy_image_first_sector(image, 1, 0, &id));
    assert(id == first_sector_id);
    assert(floppy_image_next_sector(
               image, 1, 0, first_sector_id, &id));
    assert(id == first_sector_id + 1);
    assert(floppy_image_read_sector(
               image, 1, 0, first_sector_id + 2, sector) == 0);
    for (size_t index = 0; index < sizeof(sector); ++index)
        assert(sector[index] == 0x2b);
    memset(replacement, 0x5a, sizeof(replacement));
    assert(floppy_image_write_sector(
               image, 0, 0, first_sector_id + 8, replacement) == 0);
    assert(floppy_image_flush(image) == 0);
    memset(sector, 0, sizeof(sector));
    assert(floppy_image_read_sector(
               image, 0, 0, first_sector_id + 8, sector) == 0);
    assert(memcmp(sector, replacement, sizeof(sector)) == 0);
    assert(floppy_image_eject(image) == 0);
    assert(remove(path) == 0);
}

static void corrupt_raw_fixture_as_cpc(void) {
    FILE *file = fopen(fixture_path, "r+b");
    static const char magic[] =
        "EXTENDED CPC DSK File\r\nDisk-Info\r\n";

    assert(file);
    assert(fwrite(magic, 1, sizeof(magic) - 1, file) ==
           sizeof(magic) - 1);
    assert(fclose(file) == 0);
}

int main(void) {
    FloppyImage image;
    u8 sector[FLOPPY_SECTOR_SIZE];
    u8 replacement[FLOPPY_SECTOR_SIZE];

    create_fixture();
    floppy_image_init(&image);
    assert(!floppy_image_mounted(&image));
    assert(floppy_image_mount(
               &image, fixture_path,
               FLOPPY_IMAGE_READ_ONLY) == 0);
    assert(floppy_image_mounted(&image));
    assert(!floppy_image_writable(&image));
    assert(image.tracks == 80);
    assert(image.sides == 2);
    assert(image.sectors_per_track == 9);
    assert(floppy_image_take_disk_changed(&image));
    assert(!floppy_image_take_disk_changed(&image));

    assert(floppy_image_read_sector(&image, 1, 1, 3, sector) == 0);
    assert_sector_pattern(sector, 29);
    assert(floppy_image_take_activity(&image));
    assert(!floppy_image_take_activity(&image));
    memset(replacement, 0xa5, sizeof(replacement));
    assert(floppy_image_write_sector(
               &image, 1, 1, 3, replacement) == -1);
    assert(!floppy_image_dirty(&image));
    assert(floppy_image_mount(
               &image, "missing-floppy-image.tmp",
               FLOPPY_IMAGE_READ_ONLY) == -1);
    assert(floppy_image_mounted(&image));
    assert(floppy_image_error(&image)[0]);

    assert(floppy_image_mount(
               &image, fixture_path,
               FLOPPY_IMAGE_READ_WRITE) == 0);
    assert(floppy_image_writable(&image));
    assert(floppy_image_write_sector(
               &image, 1, 1, 3, replacement) == 0);
    assert(floppy_image_dirty(&image));
    assert(floppy_image_flush(&image) == 0);
    assert(!floppy_image_dirty(&image));
    memset(sector, 0, sizeof(sector));
    assert(floppy_image_read_sector(&image, 1, 1, 3, sector) == 0);
    assert(memcmp(sector, replacement, sizeof(sector)) == 0);
    assert(floppy_image_read_sector(&image, 80, 0, 1, sector) == -1);
    assert(floppy_image_read_sector(&image, 0, 2, 1, sector) == -1);
    assert(floppy_image_read_sector(&image, 0, 0, 10, sector) == -1);

    assert(floppy_image_eject(&image) == 0);
    assert(!floppy_image_mounted(&image));
    assert(floppy_image_take_disk_changed(&image));

    test_cpc_dsk_container(
        &image, standard_dsk_path, false, 1);
    test_cpc_dsk_container(
        &image, extended_dsk_path, true, 0xc1);

    /* A recognized but malformed CPCEMU image must not fall through to
     * raw-size detection, even when its host file happens to be 720 KiB. */
    create_fixture();
    corrupt_raw_fixture_as_cpc();
    assert(floppy_image_mount(
               &image, fixture_path,
               FLOPPY_IMAGE_READ_ONLY) == -1);
    assert(!floppy_image_mounted(&image));

    /*
     * A disappearing host device must leave a dirty image attached so the
     * user is warned instead of receiving a false successful ejection.
     */
    create_fixture();
    assert(floppy_image_mount(
               &image, fixture_path,
               FLOPPY_IMAGE_READ_WRITE) == 0);
    assert(floppy_image_write_sector(
               &image, 0, 0, 2, replacement) == 0);
    assert(floppy_image_dirty(&image));
    assert(TEST_CLOSE(TEST_FILENO(image.file)) == 0);
    assert(floppy_image_flush(&image) == -1);
    assert(floppy_image_eject(&image) == -1);
    assert(floppy_image_mounted(&image));
    assert(floppy_image_dirty(&image));
    assert(floppy_image_has_error(&image));
    assert(strstr(floppy_image_error(&image), "flush"));
    floppy_image_destroy(&image);
    assert(remove(fixture_path) == 0);
    return 0;
}
