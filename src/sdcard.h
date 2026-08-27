#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "types.h"

#define SD_CARD_SECTOR_SIZE 512u
#define SD_CARD_RESPONSE_CAPACITY 1024u
#define SD_CARD_ERROR_MAX 192u

typedef enum {
    SD_IMAGE_READ_ONLY = 0,
    SD_IMAGE_READ_WRITE
} SdImageMode;

typedef struct {
    FILE *image;
    u64 sector_count;
    SdImageMode image_mode;
    bool dirty;
    bool io_error;
    bool activity;
    bool media_changed;
    char host_error[SD_CARD_ERROR_MAX];

    bool selected;
    bool idle;
    bool high_capacity;
    bool force_high_capacity;
    bool app_command;
    u8 command[6];
    unsigned command_length;

    u8 response[SD_CARD_RESPONSE_CAPACITY];
    size_t response_head;
    size_t response_count;

    u8 sector[SD_CARD_SECTOR_SIZE];
    u32 transfer_lba;
    size_t transfer_offset;
    unsigned crc_bytes;
    bool multi_read;
    bool write_wait_token;
    bool multi_write;
} SdCard;

void sd_card_init(SdCard *card);
void sd_card_destroy(SdCard *card);
void sd_card_reset(SdCard *card);

int sd_card_mount(SdCard *card, const char *path, SdImageMode mode);
int sd_card_flush(SdCard *card);
int sd_card_eject(SdCard *card);
bool sd_card_mounted(const SdCard *card);
bool sd_card_writable(const SdCard *card);
bool sd_card_dirty(const SdCard *card);
bool sd_card_has_error(const SdCard *card);
const char *sd_card_error(const SdCard *card);
bool sd_card_take_activity(SdCard *card);

void sd_card_select(SdCard *card, bool selected);
void sd_card_force_high_capacity(SdCard *card, bool enabled);
u8 sd_card_transfer(SdCard *card, u8 value);
