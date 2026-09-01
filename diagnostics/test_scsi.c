#include "msx_scsi.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *fixture_path = "test-scsi-image.tmp";

static u8 io_read(MsxScsi *scsi, u8 port) {
    u8 value = 0xff;

    assert(msx_scsi_io_read(scsi, port, &value));
    return value;
}

static void io_write(MsxScsi *scsi, u8 port, u8 value) {
    assert(msx_scsi_io_write(scsi, port, value));
}

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");

    assert(file);
    for (unsigned sector = 0; sector < 4; ++sector)
        for (unsigned i = 0; i < SCSI_DISK_SECTOR_SIZE; ++i)
            assert(fputc((int)(u8)(sector * 17u + i), file) != EOF);
    assert(fclose(file) == 0);
}

static void select_target(MsxScsi *scsi) {
    /* BERT SCSI asserts SEL with the initiator ID first, then adds the
     * target ID while SEL remains active.  The controller must keep
     * evaluating the data bus throughout selection. */
    io_write(scsi, 0xd0, 0x80); /* Initiator 7 only. */
    io_write(scsi, 0xd2, 0x01); /* Arbitrate. */
    assert(io_read(scsi, 0xd1) & 0x40);
    io_write(scsi, 0xd1, 0x05); /* Data bus + SEL. */
    io_write(scsi, 0xd0, 0x81); /* Initiator 7, target 0. */
    assert((io_read(scsi, 0xd4) & 0x42) == 0x42);
    io_write(scsi, 0xd2, 0x00);
    io_write(scsi, 0xd1, 0x00); /* Release SEL and data. */
    assert((io_read(scsi, 0xd4) & 0x68) == 0x68);
}

static void pio_send(MsxScsi *scsi, u8 value) {
    io_write(scsi, 0xd0, value);
    io_write(scsi, 0xd1, 0x01);
    io_write(scsi, 0xd1, 0x11);
    assert(!(io_read(scsi, 0xd4) & 0x20));
    io_write(scsi, 0xd1, 0x01);
}

static u8 pio_receive(MsxScsi *scsi) {
    u8 value = io_read(scsi, 0xd0);

    io_write(scsi, 0xd1, 0x10);
    assert(!(io_read(scsi, 0xd4) & 0x20));
    io_write(scsi, 0xd1, 0x00);
    return value;
}

static void send_cdb(MsxScsi *scsi, const u8 *cdb, size_t length) {
    for (size_t i = 0; i < length; ++i)
        pio_send(scsi, cdb[i]);
}

static void finish_status(MsxScsi *scsi, u8 expected) {
    assert((io_read(scsi, 0xd4) & 0x7c) == 0x6c);
    assert(pio_receive(scsi) == expected);
    assert((io_read(scsi, 0xd4) & 0x7c) == 0x7c);
    assert(pio_receive(scsi) == 0x00);
    assert((io_read(scsi, 0xd4) & 0x60) == 0x00);
}

int main(void) {
    MsxScsi scsi;
    u8 rom[4 * MSX_SCSI_ROM_BANK_SIZE];
    static const u8 inquiry[6] = { 0x12, 0, 0, 0, 36, 0 };
    static const u8 read6[6] = { 0x08, 0, 0, 1, 1, 0 };
    static const u8 write6[6] = { 0x0a, 0, 0, 2, 1, 0 };

    for (unsigned bank = 0; bank < 4; ++bank)
        memset(&rom[bank * MSX_SCSI_ROM_BANK_SIZE],
               (int)bank, MSX_SCSI_ROM_BANK_SIZE);
    create_fixture();
    msx_scsi_init(&scsi);
    assert(msx_scsi_target_id(&scsi) == 0);
    assert(msx_scsi_io_base(&scsi) == MSX_SCSI_IO_BASE_D0);
    assert(msx_scsi_memory_read(&scsi, 0x4000) == 0xff);
    assert(msx_scsi_install_rom(&scsi, rom, sizeof(rom)) == 0);
    assert(msx_scsi_memory_read(&scsi, 0x4000) == 0);

    {
        u8 ignored = 0xff;

        assert(!msx_scsi_io_read(&scsi, 0x30, &ignored));
        msx_scsi_set_io_base(&scsi, MSX_SCSI_IO_BASE_30);
        assert(msx_scsi_io_base(&scsi) == MSX_SCSI_IO_BASE_30);
        io_write(&scsi, 0x31, 0x40);
        assert(io_read(&scsi, 0x30) == 0xff);
        assert(!msx_scsi_io_read(&scsi, 0xd0, &ignored));
        msx_scsi_set_io_base(&scsi, MSX_SCSI_IO_BASE_D0);
    }
    msx_scsi_memory_write(&scsi, 0x6000, 3);
    assert(msx_scsi_memory_read(&scsi, 0x4000) == 3);
    msx_scsi_memory_write(&scsi, 0x6001, 1);
    assert(msx_scsi_memory_read(&scsi, 0x4000) == 3);
    msx_scsi_reset(&scsi);
    assert(msx_scsi_memory_read(&scsi, 0x4000) == 0);

    /* Z5380 detection drives the CPU data bus to high impedance while the
     * write-only test bit is set, which reads as FFh on MSX hardware. */
    io_write(&scsi, 0xd1, 0x40);
    assert(io_read(&scsi, 0xd0) == 0xff);
    assert(io_read(&scsi, 0xd4) == 0xff);
    io_write(&scsi, 0xd1, 0x00);
    assert(io_read(&scsi, 0xd4) == 0x00);

    assert(msx_scsi_mount_disk(
               &scsi, fixture_path, ATA_IMAGE_READ_WRITE) == 0);
    assert(msx_scsi_disk_mounted(&scsi));
    assert(msx_scsi_disk_writable(&scsi));

    select_target(&scsi);
    send_cdb(&scsi, inquiry, sizeof(inquiry));
    assert((io_read(&scsi, 0xd4) & 0x6c) == 0x64);
    assert(pio_receive(&scsi) == 0x00);
    for (unsigned i = 1; i < 8; ++i)
        (void)pio_receive(&scsi);
    assert(pio_receive(&scsi) == '1');
    assert(pio_receive(&scsi) == '9');
    for (unsigned i = 10; i < 36; ++i)
        (void)pio_receive(&scsi);
    finish_status(&scsi, 0x00);

    select_target(&scsi);
    send_cdb(&scsi, read6, sizeof(read6));
    io_write(&scsi, 0xd3, 0x01); /* Expect DATA IN. */
    io_write(&scsi, 0xd2, 0x02); /* DMA mode. */
    io_write(&scsi, 0xd7, 0x00); /* Start initiator receive. */
    for (unsigned i = 0; i < SCSI_DISK_SECTOR_SIZE; ++i) {
        assert(io_read(&scsi, 0xd5) & 0x40);
        assert(io_read(&scsi, 0xd0) == (u8)(17u + i));
    }
    assert(io_read(&scsi, 0xd5) & 0x10); /* Phase mismatch IRQ. */
    io_write(&scsi, 0xd2, 0x00);
    (void)io_read(&scsi, 0xd7);
    finish_status(&scsi, 0x00);
    assert(msx_scsi_take_activity(&scsi));

    select_target(&scsi);
    send_cdb(&scsi, write6, sizeof(write6));
    io_write(&scsi, 0xd3, 0x00); /* Expect DATA OUT. */
    io_write(&scsi, 0xd2, 0x02);
    io_write(&scsi, 0xd5, 0x00); /* Start DMA send. */
    for (unsigned i = 0; i < SCSI_DISK_SECTOR_SIZE; ++i) {
        assert(io_read(&scsi, 0xd5) & 0x40);
        io_write(&scsi, 0xd0, (u8)(0xa5u ^ i));
    }
    io_write(&scsi, 0xd2, 0x00);
    (void)io_read(&scsi, 0xd7);
    finish_status(&scsi, 0x00);
    assert(msx_scsi_disk_dirty(&scsi));
    assert(msx_scsi_flush_disk(&scsi) == 0);
    assert(!msx_scsi_disk_dirty(&scsi));

    /* Resetting during an incomplete write must not touch the host image. */
    select_target(&scsi);
    send_cdb(&scsi, write6, sizeof(write6));
    io_write(&scsi, 0xd3, 0x00);
    io_write(&scsi, 0xd2, 0x02);
    io_write(&scsi, 0xd5, 0x00);
    io_write(&scsi, 0xd0, 0x11);
    msx_scsi_reset(&scsi);
    assert(!msx_scsi_disk_dirty(&scsi));
    assert(msx_scsi_eject_disk(&scsi) == 0);
    assert(msx_scsi_eject_rom(&scsi) == 0);
    msx_scsi_destroy(&scsi);

    {
        FILE *file = fopen(fixture_path, "rb");

        assert(file);
        assert(fseek(file, 2 * SCSI_DISK_SECTOR_SIZE, SEEK_SET) == 0);
        for (unsigned i = 0; i < SCSI_DISK_SECTOR_SIZE; ++i)
            assert(fgetc(file) == (int)(u8)(0xa5u ^ i));
        assert(fclose(file) == 0);
    }
    assert(remove(fixture_path) == 0);
    return 0;
}
