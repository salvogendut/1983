#include "sd_mapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 sd_command(MsxSdMapper *mapper, u16 transfer_address,
                     u8 number, u32 argument) {
    const u8 bytes[6] = {
        (u8)(0x40 | number),
        (u8)(argument >> 24),
        (u8)(argument >> 16),
        (u8)(argument >> 8),
        (u8)argument,
        0x95
    };

    for (unsigned i = 0; i < sizeof(bytes); ++i)
        sd_mapper_write(mapper, transfer_address, bytes[i]);
    for (unsigned i = 0; i < 32; ++i) {
        u8 response = sd_mapper_read(mapper, transfer_address);

        if (!(response & 0x80))
            return response;
    }
    assert(!"SD command did not return an R1 response");
    return 0xff;
}

static void sd_write_payload(MsxSdMapper *mapper, u16 transfer_address,
                             u8 token, u8 value) {
    sd_mapper_write(mapper, transfer_address, token);
    for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
        sd_mapper_write(mapper, transfer_address, value);
    sd_mapper_write(mapper, transfer_address, 0xff);
    sd_mapper_write(mapper, transfer_address, 0xff);
    assert(sd_mapper_read(mapper, transfer_address) == 0xff);
    assert((sd_mapper_read(mapper, transfer_address) & 0x1f) == 0x05);
    assert(sd_mapper_read(mapper, transfer_address) == 0x00);
    assert(sd_mapper_read(mapper, transfer_address) == 0xff);
}

static void sd_write_sector(MsxSdMapper *mapper, u16 transfer_address,
                            u32 lba, u8 value) {
    assert(sd_command(mapper, transfer_address, 24, lba) == 0x00);
    sd_write_payload(mapper, transfer_address, 0xfe, value);
}

int main(void) {
    static const char *card_path = "test-sd-mapper-card.tmp";
    static const char *compat_path = "test-sd-mapper-megasd.tmp";
    MsxSdMapper mapper;
    u8 *rom = malloc(MSX_SD_MAPPER_ROM_SIZE);
    FILE *file;

    assert(rom);
    for (unsigned bank = 0;
         bank < MSX_SD_MAPPER_ROM_SIZE /
                    MSX_SD_MAPPER_ROM_BANK_SIZE;
         ++bank) {
        memset(rom + bank * MSX_SD_MAPPER_ROM_BANK_SIZE,
               bank, MSX_SD_MAPPER_ROM_BANK_SIZE);
    }

    sd_mapper_init(&mapper);
    assert(sd_mapper_install_rom(
               &mapper, rom, MSX_SD_MAPPER_ROM_SIZE) == 0);
    assert(sd_mapper_slot_expanded(&mapper));
    assert(sd_mapper_secondary_read(&mapper) == 0xff);
    assert(sd_mapper_read(&mapper, 0x4000) == 0);
    assert(sd_mapper_read(&mapper, 0x8000) == 0xff);

    sd_mapper_write(&mapper, 0x6000, 6);
    assert(sd_mapper_read(&mapper, 0x4000) == 6);
    sd_mapper_write(&mapper, 0x7000, 0x0b);
    assert(sd_mapper_read(&mapper, 0x8000) == 3);
    sd_mapper_set_alternate_driver(&mapper, true);
    assert(sd_mapper_read(&mapper, 0x4000) == 14);
    assert(sd_mapper_read(&mapper, 0x8000) == 11);

    sd_mapper_secondary_write(&mapper, 0x55);
    assert(sd_mapper_secondary_read(&mapper) == 0xaa);
    assert(sd_mapper_selected_subslot(&mapper, 0x0000) == 1);
    assert(sd_mapper_selected_subslot(&mapper, 0x4000) == 1);
    assert(sd_mapper_selected_subslot(&mapper, 0x8000) == 1);
    assert(sd_mapper_selected_subslot(&mapper, 0xc000) == 1);
    sd_mapper_write(&mapper, 0x4000, 0x83);
    assert(sd_mapper_read(&mapper, 0x4000) == 0x83);
    assert(sd_mapper_io_read(&mapper, 0) == 0xe3);
    sd_mapper_io_write(&mapper, 0, 31);
    assert(sd_mapper_io_read(&mapper, 0) == 0xff);
    sd_mapper_write(&mapper, 0x4000, 0x19);
    assert(sd_mapper_read(&mapper, 0x4000) == 0x19);

    file = fopen(compat_path, "wb");
    assert(file);
    for (unsigned sector = 0; sector < 256; ++sector)
        for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
            assert(fputc(sector == 0 ? 0x11 : 0x22, file) != EOF);
    assert(fclose(file) == 0);
    assert(sd_mapper_mount_card(
               &mapper, 0, compat_path, SD_IMAGE_READ_WRITE) == 0);

    /*
     * SD Mapper V2 exposes SDHC cards even when its firmware's ACMD41 omits
     * the HCS request. A regression divided LBA 164 by 512 and overwrote the
     * MBR while the SymbOS installer was copying files through Nextor.
     */
    sd_mapper_set_alternate_driver(&mapper, false);
    sd_mapper_secondary_write(&mapper, 0);
    sd_mapper_write(&mapper, 0x6000, 7);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    assert(sd_command(&mapper, 0x7b00, 0, 0) == 0x01);
    assert(sd_command(&mapper, 0x7b00, 55, 0) == 0x01);
    assert(sd_command(&mapper, 0x7b00, 41, 0) == 0x00);
    assert(sd_command(&mapper, 0x7b00, 58, 0) == 0x00);
    assert(sd_mapper_read(&mapper, 0x7b00) == 0x40);
    (void)sd_mapper_read(&mapper, 0x7b00);
    (void)sd_mapper_read(&mapper, 0x7b00);
    (void)sd_mapper_read(&mapper, 0x7b00);
    sd_write_sector(&mapper, 0x7b00, 164, 0x5a);

    /* A fresh CMD0 restarts the standard SD initialization handshake. */
    assert(sd_command(&mapper, 0x7b00, 0, 0) == 0x01);
    assert(sd_command(&mapper, 0x7b00, 0, 0) == 0x01);
    assert(sd_command(&mapper, 0x7b00, 8, 0x1aa) == 0x01);
    assert(sd_mapper_read(&mapper, 0x7b00) == 0x02);
    assert(sd_mapper_read(&mapper, 0x7b00) == 0x00);
    assert(sd_mapper_read(&mapper, 0x7b00) == 0x01);
    assert(sd_mapper_read(&mapper, 0x7b00) == 0xaa);
    assert(sd_command(&mapper, 0x7b00, 55, 0) == 0x01);
    assert(sd_command(&mapper, 0x7b00, 41, 0x40000000) == 0x00);
    assert(sd_command(&mapper, 0x7b00, 55, 0) == 0x00);
    assert(sd_command(&mapper, 0x7b00, 23, 2) == 0x00);
    assert(sd_command(&mapper, 0x7b00, 25, 165) == 0x00);
    sd_write_payload(&mapper, 0x7b00, 0xfc, 0xa5);
    sd_write_payload(&mapper, 0x7b00, 0xfc, 0x3c);
    sd_mapper_write(&mapper, 0x7b00, 0xfd);
    assert(sd_mapper_read(&mapper, 0x7b00) == 0xff);

    assert(sd_mapper_flush_card(&mapper, 0) == 0);
    assert(sd_mapper_eject_card(&mapper, 0) == 0);

    file = fopen(compat_path, "rb");
    assert(file);
    for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
        assert(fgetc(file) == 0x11);
    assert(fseek(file, 164 * SD_CARD_SECTOR_SIZE, SEEK_SET) == 0);
    for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
        assert(fgetc(file) == 0x5a);
    for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
        assert(fgetc(file) == 0xa5);
    for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
        assert(fgetc(file) == 0x3c);
    assert(fclose(file) == 0);

    /* High bank bits are ignored by the real SD Mapper V2 ROM mapper. */
    sd_mapper_secondary_write(&mapper, 0);
    sd_mapper_write(&mapper, 0x6000, 0x40);
    assert(sd_mapper_read(&mapper, 0x4000) == 0);
    assert(!mapper.cards[0].selected);

    sd_mapper_set_alternate_driver(&mapper, true);
    sd_mapper_secondary_write(&mapper, 0);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    (void)sd_mapper_read(&mapper, 0x7ff0);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x06);

    file = fopen(card_path, "wb");
    assert(file);
    for (unsigned i = 0; i < 2 * SD_CARD_SECTOR_SIZE; ++i)
        assert(fputc(0, file) != EOF);
    assert(fclose(file) == 0);
    assert(sd_mapper_mount_card(
               &mapper, 0, card_path, SD_IMAGE_READ_ONLY) == 0);
    assert(sd_mapper_mount_card(
               &mapper, 1, card_path, SD_IMAGE_READ_WRITE) == 0);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x05);
    sd_mapper_write(&mapper, 0x7ff0, 2);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x01);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x04);

    sd_mapper_write(&mapper, 0x7ff1, 2);
    assert(sd_mapper_read(&mapper, 0x7ff1) == 2);
    sd_mapper_tick(&mapper, 37, 3579545);
    assert(sd_mapper_read(&mapper, 0x7ff1) < 2);

    sd_mapper_reset(&mapper);
    assert(sd_mapper_io_read(&mapper, 0) == 0xe3);
    assert(sd_mapper_io_read(&mapper, 1) == 0xe2);
    assert(sd_mapper_io_read(&mapper, 2) == 0xe1);
    assert(sd_mapper_io_read(&mapper, 3) == 0xe0);
    assert(sd_mapper_secondary_read(&mapper) == 0xff);

    sd_mapper_set_mapper_enabled(&mapper, false);
    assert(!sd_mapper_slot_expanded(&mapper));
    assert(sd_mapper_io_read(&mapper, 0) == 0xff);
    assert(sd_mapper_read(&mapper, 0x4000) == 8);

    assert(sd_mapper_eject_rom(&mapper) == 0);
    assert(!sd_mapper_card_mounted(&mapper, 0));
    assert(!sd_mapper_card_mounted(&mapper, 1));
    sd_mapper_destroy(&mapper);
    assert(remove(card_path) == 0);
    assert(remove(compat_path) == 0);
    free(rom);
    return 0;
}
