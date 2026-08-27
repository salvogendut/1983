#include "sdcard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *fixture_path = "test-sdcard-image.tmp";

static void create_fixture(void) {
    FILE *file = fopen(fixture_path, "wb");
    u8 sector[SD_CARD_SECTOR_SIZE];

    assert(file);
    for (unsigned block = 0; block < 2048; ++block) {
        for (unsigned i = 0; i < sizeof(sector); ++i)
            sector[i] = (u8)(block ^ i);
        assert(fwrite(sector, 1, sizeof(sector), file) ==
               sizeof(sector));
    }
    assert(fclose(file) == 0);
}

static void send_command(SdCard *card, u8 number, u32 argument) {
    u8 bytes[6] = {
        (u8)(0x40 | number),
        (u8)(argument >> 24),
        (u8)(argument >> 16),
        (u8)(argument >> 8),
        (u8)argument,
        0x95
    };

    for (unsigned i = 0; i < sizeof(bytes); ++i)
        (void)sd_card_transfer(card, bytes[i]);
}

static u8 command(SdCard *card, u8 number, u32 argument) {
    send_command(card, number, argument);
    for (unsigned i = 0; i < 32; ++i) {
        u8 response = sd_card_transfer(card, 0xff);

        if (!(response & 0x80))
            return response;
    }
    assert(!"SD command did not return an R1 response");
    return 0xff;
}

static void initialize_card(SdCard *card, bool high_capacity) {
    assert(command(card, 0, 0) == 0x01);
    assert(command(card, 8, 0x1aa) == 0x01);
    assert(sd_card_transfer(card, 0xff) == 0x02);
    assert(sd_card_transfer(card, 0xff) == 0x00);
    assert(sd_card_transfer(card, 0xff) == 0x01);
    assert(sd_card_transfer(card, 0xff) == 0xaa);
    assert(command(card, 55, 0) == 0x01);
    assert(command(card, 41,
                   high_capacity ? 0x40000000u : 0) == 0x00);
    assert(command(card, 58, 0) == 0x00);
    assert(sd_card_transfer(card, 0xff) ==
           (high_capacity ? 0x40 : 0x00));
    assert(sd_card_transfer(card, 0xff) == 0xff);
    assert(sd_card_transfer(card, 0xff) == 0x80);
    assert(sd_card_transfer(card, 0xff) == 0x00);
}

static void receive_data_block(SdCard *card, u8 *data, size_t size) {
    for (unsigned i = 0;; ++i) {
        u8 token = sd_card_transfer(card, 0xff);

        assert(i < 32);
        if (token == 0xfe)
            break;
    }
    for (size_t i = 0; i < size; ++i)
        data[i] = sd_card_transfer(card, 0xff);
    (void)sd_card_transfer(card, 0xff);
    (void)sd_card_transfer(card, 0xff);
}

static void read_block(SdCard *card, u32 address, u8 data[512]) {
    assert(command(card, 17, address) == 0x00);
    receive_data_block(card, data, SD_CARD_SECTOR_SIZE);
}

static void send_write_block(SdCard *card, u8 token,
                             const u8 data[512]) {
    (void)sd_card_transfer(card, token);
    for (unsigned i = 0; i < SD_CARD_SECTOR_SIZE; ++i)
        (void)sd_card_transfer(card, data[i]);
    (void)sd_card_transfer(card, 0xff);
    (void)sd_card_transfer(card, 0xff);
    assert(sd_card_transfer(card, 0xff) == 0xff);
    assert((sd_card_transfer(card, 0xff) & 0x1f) == 0x05);
    assert(sd_card_transfer(card, 0xff) == 0x00);
    assert(sd_card_transfer(card, 0xff) == 0xff);
}

int main(void) {
    SdCard card;
    u8 data[SD_CARD_SECTOR_SIZE];
    u8 second[SD_CARD_SECTOR_SIZE];
    u8 register_data[16];
    FILE *file;

    create_fixture();
    sd_card_init(&card);
    assert(sd_card_mount(&card, fixture_path,
                         SD_IMAGE_READ_ONLY) == 0);
    assert(sd_card_mounted(&card));
    assert(!sd_card_writable(&card));
    assert(card.media_changed);
    sd_card_select(&card, true);
    initialize_card(&card, true);

    /* Commands take two SPI transfers before their response, like openMSX. */
    send_command(&card, 13, 0);
    assert(sd_card_transfer(&card, 0xff) == 0xff);
    assert(sd_card_transfer(&card, 0xff) == 0xff);
    assert(sd_card_transfer(&card, 0xff) == 0x00);
    assert(sd_card_transfer(&card, 0xff) == 0x00);

    assert(command(&card, 9, 0) == 0x00);
    receive_data_block(&card, register_data, sizeof(register_data));
    assert((register_data[0] & 0xc0) == 0x40);
    assert(command(&card, 10, 0) == 0x00);
    receive_data_block(&card, register_data, sizeof(register_data));
    assert(register_data[0] == 0x83);

    read_block(&card, 3, data);
    for (unsigned i = 0; i < sizeof(data); ++i)
        assert(data[i] == (u8)(3 ^ i));
    assert(command(&card, 18, 10) == 0x00);
    for (unsigned i = 0;; ++i) {
        u8 token = sd_card_transfer(&card, 0xff);

        assert(i < 32);
        if (token == 0xfe)
            break;
    }
    for (unsigned i = 0; i < 100; ++i)
        data[i] = sd_card_transfer(&card, 0xff);
    sd_card_select(&card, false);
    assert(sd_card_transfer(&card, 0xff) == 0xff);
    sd_card_select(&card, true);
    for (unsigned i = 100; i < sizeof(data); ++i)
        data[i] = sd_card_transfer(&card, 0xff);
    (void)sd_card_transfer(&card, 0xff);
    (void)sd_card_transfer(&card, 0xff);
    receive_data_block(&card, second, sizeof(second));
    for (unsigned i = 0; i < sizeof(data); ++i) {
        assert(data[i] == (u8)(10 ^ i));
        assert(second[i] == (u8)(11 ^ i));
    }
    assert(command(&card, 12, 0) == 0x00);
    assert(sd_card_take_activity(&card));
    assert(!sd_card_take_activity(&card));

    assert(command(&card, 24, 3) == 0x04);
    assert(!sd_card_dirty(&card));
    assert(sd_card_eject(&card) == 0);

    assert(sd_card_mount(&card, fixture_path,
                         SD_IMAGE_READ_WRITE) == 0);
    sd_card_select(&card, true);
    initialize_card(&card, true);
    assert(command(&card, 24, 5) == 0x00);
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = (u8)(0xa5 ^ i);
    send_write_block(&card, 0xfe, data);

    assert(command(&card, 25, 20) == 0x00);
    for (unsigned i = 0; i < sizeof(data); ++i) {
        data[i] = (u8)(0x20 ^ i);
        second[i] = (u8)(0x21 ^ i);
    }
    send_write_block(&card, 0xfc, data);
    send_write_block(&card, 0xfc, second);
    (void)sd_card_transfer(&card, 0xfd);
    assert(sd_card_dirty(&card));
    assert(sd_card_flush(&card) == 0);
    assert(!sd_card_dirty(&card));
    assert(sd_card_eject(&card) == 0);

    file = fopen(fixture_path, "rb");
    assert(file);
    assert(fseek(file, 5 * SD_CARD_SECTOR_SIZE, SEEK_SET) == 0);
    assert(fread(data, 1, sizeof(data), file) == sizeof(data));
    assert(fclose(file) == 0);
    for (unsigned i = 0; i < sizeof(data); ++i)
        assert(data[i] == (u8)(0xa5 ^ i));

    file = fopen(fixture_path, "rb");
    assert(file);
    assert(fseek(file, 20 * SD_CARD_SECTOR_SIZE, SEEK_SET) == 0);
    assert(fread(data, 1, sizeof(data), file) == sizeof(data));
    assert(fread(second, 1, sizeof(second), file) == sizeof(second));
    assert(fclose(file) == 0);
    for (unsigned i = 0; i < sizeof(data); ++i) {
        assert(data[i] == (u8)(0x20 ^ i));
        assert(second[i] == (u8)(0x21 ^ i));
    }

    /*
     * With CCS clear, the same SPI endpoint accepts conventional SDSC byte
     * addresses instead of SDHC sector addresses.
     */
    assert(sd_card_mount(&card, fixture_path,
                         SD_IMAGE_READ_ONLY) == 0);
    sd_card_select(&card, true);
    initialize_card(&card, false);
    read_block(&card, 7 * SD_CARD_SECTOR_SIZE, data);
    for (unsigned i = 0; i < sizeof(data); ++i)
        assert(data[i] == (u8)(7 ^ i));

    /* A failed replacement is conservative and leaves the old card live. */
    assert(sd_card_mount(&card, "missing-sdcard-image.tmp",
                         SD_IMAGE_READ_ONLY) != 0);
    assert(sd_card_mounted(&card));
    assert(sd_card_error(&card)[0]);

    assert(sd_card_eject(&card) == 0);
    assert(!sd_card_mounted(&card));
    assert(sd_card_mount(&card, fixture_path,
                         SD_IMAGE_READ_WRITE) == 0);
    sd_card_select(&card, true);
    initialize_card(&card, true);
    assert(command(&card, 24, 6) == 0x00);
    (void)sd_card_transfer(&card, 0xfe);
    for (unsigned i = 0; i < 100; ++i)
        (void)sd_card_transfer(&card, 0xee);
    sd_card_reset(&card);
    assert(sd_card_mounted(&card));
    assert(!sd_card_dirty(&card));
    sd_card_select(&card, true);
    initialize_card(&card, true);
    read_block(&card, 6, data);
    for (unsigned i = 0; i < sizeof(data); ++i)
        assert(data[i] == (u8)(6 ^ i));
    assert(sd_card_eject(&card) == 0);
    sd_card_destroy(&card);
    assert(remove(fixture_path) == 0);
    return 0;
}
