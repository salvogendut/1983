#include "wd2793.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *fixture_path = "test-wd2793-image.tmp";

static u8 original_byte(size_t offset) {
    return (u8)(offset * 11u + 5u);
}

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");
    u8 sector[FLOPPY_SECTOR_SIZE];

    assert(file);
    for (unsigned logical = 0; logical < 1440; ++logical) {
        for (size_t i = 0; i < sizeof(sector); ++i)
            sector[i] = original_byte(
                (size_t)logical * sizeof(sector) + i);
        if (logical == 0) {
            sector[11] = 0;
            sector[12] = 2;
            sector[19] = 0xa0;
            sector[20] = 5;
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

static void select_drive_a(Wd2793 *fdc) {
    wd2793_write_memory(fdc, 0x7ffd, 0x80);
}

static void read_sector(Wd2793 *fdc, u8 *data) {
    wd2793_write_memory(fdc, 0x7ff8, 0x80);
    assert((wd2793_read_memory(fdc, 0x7fff) & 0x80) == 0);
    assert(wd2793_read_memory(fdc, 0x7ff8) &
           WD2793_STATUS_BUSY);
    for (size_t i = 0; i < FLOPPY_SECTOR_SIZE; ++i)
        data[i] = wd2793_read_memory(fdc, 0x7ffb);
    assert((wd2793_read_memory(fdc, 0x7fff) & 0x40) == 0);
    assert(!(wd2793_read_memory(fdc, 0x7ff8) &
             WD2793_STATUS_BUSY));
    assert(wd2793_read_memory(fdc, 0x7fff) & 0x40);
}

int main(void) {
    Wd2793 fdc;
    u8 data[FLOPPY_SECTOR_SIZE];

    create_fixture();
    wd2793_init(&fdc);
    assert(wd2793_handles_address(0x3ff8));
    assert(wd2793_handles_address(0x7fff));
    assert(!wd2793_handles_address(0x7ff7));
    assert(wd2793_read_memory(&fdc, 0x7ff8) &
           WD2793_STATUS_NOT_READY);

    assert(wd2793_mount_drive_a(
               &fdc, fixture_path,
               FLOPPY_IMAGE_READ_ONLY) == 0);
    select_drive_a(&fdc);
    assert((wd2793_read_memory(&fdc, 0x7ffd) & 4) == 0);
    assert(wd2793_read_memory(&fdc, 0x7ffd) & 4);
    assert(wd2793_read_memory(&fdc, 0x7ff8) & WD2793_STATUS_INDEX);
    wd2793_advance(&fdc, WD2793_INDEX_PULSE_CYCLES);
    assert(!(wd2793_read_memory(&fdc, 0x7ff8) & WD2793_STATUS_INDEX));
    wd2793_advance(
        &fdc, WD2793_INDEX_PERIOD_CYCLES - WD2793_INDEX_PULSE_CYCLES);
    assert(wd2793_read_memory(&fdc, 0x7ff8) & WD2793_STATUS_INDEX);
    wd2793_write_memory(&fdc, 0x7ff9, 1);
    wd2793_write_memory(&fdc, 0x7ffa, 3);
    wd2793_write_memory(&fdc, 0x7ffc, 1);
    fdc.physical_track = 1;
    read_sector(&fdc, data);
    for (size_t i = 0; i < sizeof(data); ++i)
        assert(data[i] ==
               original_byte((size_t)29 * sizeof(data) + i));
    assert(wd2793_take_drive_a_activity(&fdc));
    assert(!wd2793_take_drive_a_activity(&fdc));

    assert(wd2793_mount_drive_b(
               &fdc, fixture_path,
               FLOPPY_IMAGE_READ_ONLY) == 0);
    wd2793_write_memory(&fdc, 0x7ffd, 0x81);
    assert((wd2793_read_memory(&fdc, 0x7ffd) & 4) == 0);
    wd2793_write_memory(&fdc, 0x7ff9, 0);
    wd2793_write_memory(&fdc, 0x7ffa, 2);
    wd2793_write_memory(&fdc, 0x7ffc, 0);
    fdc.physical_track = 0;
    read_sector(&fdc, data);
    for (size_t i = 0; i < sizeof(data); ++i)
        assert(data[i] == original_byte(sizeof(data) + i));
    assert(wd2793_take_drive_b_activity(&fdc));
    assert(!wd2793_take_drive_a_activity(&fdc));

    select_drive_a(&fdc);
    wd2793_write_memory(&fdc, 0x7ffb, 7);
    wd2793_write_memory(&fdc, 0x7ff8, 0x10);
    assert(wd2793_read_memory(&fdc, 0x7ff9) == 7);
    assert(fdc.physical_track == 7);
    wd2793_write_memory(&fdc, 0x7ff8, 0x00);
    assert(wd2793_read_memory(&fdc, 0x7ff9) == 0);
    assert(wd2793_read_memory(&fdc, 0x7ff8) &
           WD2793_STATUS_TRACK_ZERO);

    wd2793_write_memory(&fdc, 0x7ffa, 1);
    wd2793_write_memory(&fdc, 0x7ff8, 0xa0);
    assert((wd2793_read_memory(&fdc, 0x7fff) & 0x40) == 0);
    assert(wd2793_read_memory(&fdc, 0x7ff8) &
           WD2793_STATUS_WRITE_PROTECT);

    assert(wd2793_mount_drive_a(
               &fdc, fixture_path,
               FLOPPY_IMAGE_READ_WRITE) == 0);
    select_drive_a(&fdc);
    wd2793_write_memory(&fdc, 0x7ff9, 0);
    wd2793_write_memory(&fdc, 0x7ffa, 2);
    wd2793_write_memory(&fdc, 0x7ffc, 0);
    wd2793_write_memory(&fdc, 0x7ff8, 0xa0);
    assert((wd2793_read_memory(&fdc, 0x7fff) & 0x80) == 0);
    for (size_t i = 0; i < FLOPPY_SECTOR_SIZE; ++i)
        wd2793_write_memory(&fdc, 0x7ffb, (u8)(0xa5 ^ i));
    assert(wd2793_drive_a_dirty(&fdc));
    assert(wd2793_flush_drive_a(&fdc) == 0);
    wd2793_write_memory(&fdc, 0x7ff8, 0x80);
    for (size_t i = 0; i < FLOPPY_SECTOR_SIZE; ++i)
        assert(wd2793_read_memory(&fdc, 0x7ffb) ==
               (u8)(0xa5 ^ i));

    /* Reset discards a partial sector without ejecting the disk. */
    wd2793_write_memory(&fdc, 0x7ffa, 3);
    wd2793_write_memory(&fdc, 0x7ff8, 0xa0);
    for (size_t i = 0; i < FLOPPY_SECTOR_SIZE / 2; ++i)
        wd2793_write_memory(&fdc, 0x7ffb, 0x5a);
    wd2793_reset(&fdc);
    assert(wd2793_drive_a_mounted(&fdc));
    assert(wd2793_drive_b_mounted(&fdc));
    assert(!wd2793_drive_a_dirty(&fdc));

    assert(wd2793_eject_drive_a(&fdc) == 0);
    assert(wd2793_eject_drive_b(&fdc) == 0);
    assert(!wd2793_drive_a_mounted(&fdc));
    assert(!wd2793_drive_b_mounted(&fdc));
    wd2793_destroy(&fdc);
    assert(remove(fixture_path) == 0);
    return 0;
}
