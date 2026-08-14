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

static void create_sized_fixture(unsigned sectors) {
    FILE *file = fopen(fixture_path, "wb");
    u8 sector[FLOPPY_SECTOR_SIZE] = { 0 };

    assert(file);
    for (unsigned logical = 0; logical < sectors; ++logical)
        assert(fwrite(sector, 1, sizeof(sector), file) == sizeof(sector));
    assert(fclose(file) == 0);
}

static void test_raw_geometries(FloppyImage *image) {
    static const struct {
        unsigned sectors;
        unsigned tracks;
        unsigned sides;
        unsigned sectors_per_track;
    } geometries[] = {
        { 1440, 80, 2, 9 },
        { 1280, 80, 2, 8 },
        {  720, 40, 2, 9 },
        {  640, 40, 2, 8 },
        {  360, 40, 1, 9 },
        {  320, 40, 1, 8 },
    };

    for (size_t i = 0; i < sizeof(geometries) / sizeof(geometries[0]); ++i) {
        create_sized_fixture(geometries[i].sectors);
        assert(floppy_image_mount(
                   image, fixture_path, FLOPPY_IMAGE_READ_ONLY) == 0);
        assert(image->tracks == geometries[i].tracks);
        assert(image->sides == geometries[i].sides);
        assert(image->sectors_per_track == geometries[i].sectors_per_track);
        assert(floppy_image_eject(image) == 0);
    }
    assert(remove(fixture_path) == 0);
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

    test_raw_geometries(&image);

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
