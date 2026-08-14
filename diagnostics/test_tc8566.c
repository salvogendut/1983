#include "tc8566.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *fixture_path = "test-tc8566-image.tmp";

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");
    u8 sector[FLOPPY_SECTOR_SIZE] = { 0 };

    assert(file);
    sector[11] = 0x00;
    sector[12] = 0x02;
    sector[19] = 0xa0;
    sector[20] = 0x05;
    sector[24] = 9;
    sector[26] = 2;
    assert(fwrite(sector, 1, sizeof(sector), file) == sizeof(sector));
    memset(sector, 0, sizeof(sector));
    for (unsigned i = 1; i < 1440; ++i)
        assert(fwrite(sector, 1, sizeof(sector), file) == sizeof(sector));
    assert(fclose(file) == 0);
}

static void send_command(Tc8566 *fdc, u8 command,
                         const u8 *parameters, size_t count) {
    tc8566_write_data(fdc, command);
    for (size_t i = 0; i < count; ++i)
        tc8566_write_data(fdc, parameters[i]);
}

static void read_result(Tc8566 *fdc, u8 result[7]) {
    assert((tc8566_read_status(fdc) &
            (TC8566_MAIN_RQM | TC8566_MAIN_DIO)) ==
           (TC8566_MAIN_RQM | TC8566_MAIN_DIO));
    for (unsigned i = 0; i < 7; ++i)
        result[i] = tc8566_read_data(fdc);
    assert(tc8566_read_status(fdc) == TC8566_MAIN_RQM);
}

int main(void) {
    FloppyImage drive_a;
    FloppyImage drive_b;
    Tc8566 fdc;
    u8 sector[FLOPPY_SECTOR_SIZE];
    u8 result[7];

    create_fixture();
    floppy_image_init(&drive_a);
    floppy_image_init(&drive_b);
    assert(floppy_image_mount(
               &drive_a, fixture_path, FLOPPY_IMAGE_READ_WRITE) == 0);
    tc8566_init(&fdc, &drive_a, &drive_b);
    assert(tc8566_read_status(&fdc) == TC8566_MAIN_RQM);
    assert(tc8566_read_memory(&fdc, 0x8000) == TC8566_MAIN_RQM);

    tc8566_write_memory(&fdc, 0x9000, 0x14);
    assert(fdc.selected_drive == 0);
    assert(fdc.motor[0]);

    {
        const u8 parameter[] = { 0 };

        send_command(&fdc, 0x04, parameter, sizeof(parameter));
        assert(tc8566_read_data(&fdc) & 0x20);
        assert(tc8566_read_status(&fdc) == TC8566_MAIN_RQM);
    }
    {
        const u8 parameters[] = { 0, 1 };

        send_command(&fdc, 0x0f, parameters, sizeof(parameters));
        assert(tc8566_read_status(&fdc) == TC8566_MAIN_RQM);
        send_command(&fdc, 0x08, NULL, 0);
        assert((tc8566_read_data(&fdc) & 0x23) == 0x20);
        assert(tc8566_read_data(&fdc) == 1);
    }

    for (unsigned i = 0; i < sizeof(sector); ++i)
        sector[i] = (u8)(i ^ 0x5a);
    assert(floppy_image_write_sector(&drive_a, 1, 0, 2, sector) == 0);
    {
        const u8 parameters[] = { 0, 1, 0, 2, 2, 2, 0x2a, 0xff };

        send_command(&fdc, 0x46, parameters, sizeof(parameters));
        assert(tc8566_read_status(&fdc) ==
               (TC8566_MAIN_RQM | TC8566_MAIN_BUSY |
                TC8566_MAIN_NONDMA | TC8566_MAIN_DIO));
        for (unsigned i = 0; i < sizeof(sector); ++i)
            assert(tc8566_read_data(&fdc) == (u8)(i ^ 0x5a));
        read_result(&fdc, result);
        assert(result[0] == 0);
        assert(result[1] == 0);
        assert(result[5] == 3);
    }
    {
        const u8 parameters[] = { 0, 1, 0, 3, 2, 3, 0x2a, 0xff };

        send_command(&fdc, 0x45, parameters, sizeof(parameters));
        for (unsigned i = 0; i < sizeof(sector); ++i)
            tc8566_write_data(&fdc, (u8)(0xa5 ^ i));
        read_result(&fdc, result);
        assert(result[0] == 0);
        assert(floppy_image_read_sector(&drive_a, 1, 0, 3, sector) == 0);
        for (unsigned i = 0; i < sizeof(sector); ++i)
            assert(sector[i] == (u8)(0xa5 ^ i));
    }
    {
        const u8 parameters[] = { 0, 2, 2, 0x2a, 0xe5 };
        const u8 ids[] = { 1, 0, 4, 2, 1, 0, 5, 2 };

        send_command(&fdc, 0x4d, parameters, sizeof(parameters));
        for (unsigned i = 0; i < sizeof(ids); ++i)
            tc8566_write_data(&fdc, ids[i]);
        read_result(&fdc, result);
        assert(result[0] == 0);
        assert(floppy_image_read_sector(&drive_a, 1, 0, 5, sector) == 0);
        for (unsigned i = 0; i < sizeof(sector); ++i)
            assert(sector[i] == 0xe5);
    }

    assert(floppy_image_eject(&drive_a) == 0);
    assert(floppy_image_mount(
               &drive_a, fixture_path, FLOPPY_IMAGE_READ_ONLY) == 0);
    {
        const u8 parameters[] = { 0, 1, 0, 3, 2, 3, 0x2a, 0xff };

        send_command(&fdc, 0x45, parameters, sizeof(parameters));
        read_result(&fdc, result);
        assert(result[0] & 0x40);
        assert(result[1] & 0x02);
    }

    floppy_image_destroy(&drive_a);
    floppy_image_destroy(&drive_b);
    assert(remove(fixture_path) == 0);
    puts("TC8566AF diagnostics passed");
    return 0;
}
