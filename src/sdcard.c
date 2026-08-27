#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "sdcard.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define SD_FSEEK _fseeki64
#define SD_FTELL _ftelli64
#define SD_FILENO _fileno
#define SD_SYNC _commit
typedef __int64 SdOffset;
#else
#include <sys/types.h>
#include <unistd.h>
#define SD_FSEEK fseeko
#define SD_FTELL ftello
#define SD_FILENO fileno
#define SD_SYNC fsync
typedef off_t SdOffset;
#endif

static const char *sd_system_error(void) {
    return errno ? strerror(errno) : "I/O operation did not complete";
}

static void sd_host_error(SdCard *card, bool persistent,
                          const char *format, ...) {
    va_list arguments;

    if (!card)
        return;
    card->io_error |= persistent;
    va_start(arguments, format);
    vsnprintf(card->host_error, sizeof(card->host_error),
              format, arguments);
    va_end(arguments);
}

static void queue_clear(SdCard *card) {
    card->response_head = 0;
    card->response_count = 0;
}

static bool queue_push(SdCard *card, u8 value) {
    size_t index;

    if (card->response_count >= sizeof(card->response))
        return false;
    index = (card->response_head + card->response_count) %
            sizeof(card->response);
    card->response[index] = value;
    ++card->response_count;
    return true;
}

static u8 queue_pop(SdCard *card) {
    u8 value;

    if (!card->response_count)
        return 0xff;
    value = card->response[card->response_head];
    card->response_head =
        (card->response_head + 1) % sizeof(card->response);
    --card->response_count;
    return value;
}

static bool load_sector(SdCard *card, u32 lba) {
    SdOffset offset;

    if (!card->image || (u64)lba >= card->sector_count)
        return false;
    offset = (SdOffset)lba * SD_CARD_SECTOR_SIZE;
    clearerr(card->image);
    if (SD_FSEEK(card->image, offset, SEEK_SET) != 0 ||
        fread(card->sector, 1, sizeof(card->sector), card->image) !=
            sizeof(card->sector)) {
        sd_host_error(card, true,
                      "SD image read failed at sector %u: %s",
                      lba, sd_system_error());
        return false;
    }
    card->activity = true;
    return true;
}

static bool store_sector(SdCard *card, u32 lba) {
    SdOffset offset;
    size_t written;

    if (!card->image ||
        card->image_mode != SD_IMAGE_READ_WRITE ||
        (u64)lba >= card->sector_count)
        return false;
    offset = (SdOffset)lba * SD_CARD_SECTOR_SIZE;
    clearerr(card->image);
    if (SD_FSEEK(card->image, offset, SEEK_SET) != 0) {
        sd_host_error(card, true,
                      "SD image seek failed at sector %u: %s",
                      lba, sd_system_error());
        return false;
    }
    written = fwrite(card->sector, 1, sizeof(card->sector), card->image);
    if (written != sizeof(card->sector)) {
        card->dirty |= written != 0;
        sd_host_error(card, true,
                      "SD image write failed at sector %u: %s",
                      lba, sd_system_error());
        return false;
    }
    card->dirty = true;
    card->activity = true;
    return true;
}

static void queue_register_response(SdCard *card, u8 r1,
                                    const u8 *extra, size_t size) {
    queue_push(card, r1);
    for (size_t i = 0; i < size; ++i)
        queue_push(card, extra[i]);
}

static void queue_data_block(SdCard *card, const u8 *data, size_t size) {
    queue_push(card, 0xff);
    queue_push(card, 0xfe);
    for (size_t i = 0; i < size; ++i)
        queue_push(card, data[i]);
    queue_push(card, 0xff);
    queue_push(card, 0xff);
}

static void build_csd(const SdCard *card, u8 csd[16]) {
    u64 units = card->sector_count / 1024u;
    u32 c_size;

    if (!units)
        units = 1;
    if (units > 0x400000u)
        units = 0x400000u;
    c_size = (u32)(units - 1);
    memset(csd, 0, 16);
    csd[0] = 0x40;
    csd[1] = 0x0e;
    csd[2] = 0x00;
    csd[3] = 0x32;
    csd[4] = 0x5b;
    csd[5] = 0x59;
    csd[6] = (u8)(c_size >> 16) & 0x3f;
    csd[7] = (u8)(c_size >> 8);
    csd[8] = (u8)c_size;
    csd[9] = 0x7f;
    csd[10] = 0x80;
    csd[11] = 0x0a;
    csd[12] = 0x40;
    csd[13] = 0x00;
    csd[14] = 0x00;
    csd[15] = 0xff;
}

static void build_cid(u8 cid[16]) {
    static const u8 fixed_cid[16] = {
        0x83, '1', '9', '8', '3', 'S', 'D', 'M',
        0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x90, 0xff
    };

    memcpy(cid, fixed_cid, sizeof(fixed_cid));
}

static u32 command_argument(const SdCard *card) {
    return ((u32)card->command[1] << 24) |
           ((u32)card->command[2] << 16) |
           ((u32)card->command[3] << 8) |
           card->command[4];
}

static u32 command_lba(const SdCard *card) {
    u32 argument = command_argument(card);

    return (card->high_capacity || card->force_high_capacity)
         ? argument : argument / SD_CARD_SECTOR_SIZE;
}

static void begin_read(SdCard *card, bool multiple) {
    u32 lba = command_lba(card);

    if (!card->image || (u64)lba >= card->sector_count) {
        queue_push(card, 0x20);
        return;
    }
    queue_push(card, 0x00);
    card->transfer_lba = lba;
    card->multi_read = multiple;
    if (load_sector(card, lba)) {
        queue_data_block(card, card->sector, sizeof(card->sector));
        ++card->transfer_lba;
    } else {
        card->multi_read = false;
        queue_clear(card);
        queue_push(card, 0x20);
    }
}

static void execute_command(SdCard *card) {
    u8 command = card->command[0] & 0x3f;
    bool was_app = card->app_command;
    u8 idle = card->idle ? 0x01 : 0x00;

    card->app_command = false;
    card->response_delay = 2;
    switch (command) {
        case 0:
            card->idle = true;
            card->high_capacity = false;
            card->multi_read = false;
            card->write_wait_token = false;
            queue_clear(card);
            queue_push(card, 0x01);
            break;
        case 1:
            card->idle = false;
            card->high_capacity = true;
            queue_push(card, 0x00);
            break;
        case 8: {
            const u8 r7[4] = {0x02, 0x00, 0x01, 0xaa};

            queue_register_response(card, idle, r7, sizeof(r7));
            break;
        }
        case 9: {
            u8 csd[16];

            build_csd(card, csd);
            queue_push(card, idle);
            if (!card->idle)
                queue_data_block(card, csd, sizeof(csd));
            break;
        }
        case 10: {
            u8 cid[16];

            build_cid(cid);
            queue_push(card, idle);
            if (!card->idle)
                queue_data_block(card, cid, sizeof(cid));
            break;
        }
        case 12:
            card->multi_read = false;
            queue_clear(card);
            queue_push(card, 0x00);
            break;
        case 13:
            queue_push(card, idle);
            queue_push(card, 0x00);
            break;
        case 16:
            queue_push(card,
                       command_argument(card) == SD_CARD_SECTOR_SIZE
                       ? idle : (u8)(idle | 0x40));
            break;
        case 17:
            begin_read(card, false);
            break;
        case 18:
            begin_read(card, true);
            break;
        case 23:
            queue_push(card, was_app ? idle : (u8)(idle | 0x04));
            break;
        case 24:
        case 25: {
            u32 lba = command_lba(card);

            if (!card->image || (u64)lba >= card->sector_count)
                queue_push(card, 0x20);
            else if (card->image_mode != SD_IMAGE_READ_WRITE)
                queue_push(card, 0x04);
            else {
                queue_push(card, 0x00);
                card->transfer_lba = lba;
                card->transfer_offset = 0;
                card->crc_bytes = 0;
                card->write_wait_token = true;
                card->multi_write = command == 25;
            }
            break;
        }
        case 41:
            if (!was_app) {
                queue_push(card, (u8)(idle | 0x04));
            } else {
                card->idle = false;
                card->high_capacity =
                    (command_argument(card) & 0x40000000u) != 0;
                queue_push(card, 0x00);
            }
            break;
        case 55:
            card->app_command = true;
            queue_push(card, idle);
            break;
        case 58: {
            const u8 ocr[4] = {
                (card->high_capacity || card->force_high_capacity)
                    ? 0x40 : 0x00,
                0xff, 0x80, 0x00
            };

            queue_register_response(card, idle, ocr, sizeof(ocr));
            break;
        }
        default:
            queue_push(card, (u8)(idle | 0x04));
            break;
    }
}

static void parse_command_byte(SdCard *card, u8 value) {
    if (!card->command_length) {
        if ((value & 0xc0) != 0x40)
            return;
        card->command[card->command_length++] = value;
        return;
    }
    card->command[card->command_length++] = value;
    if (card->command_length == sizeof(card->command)) {
        card->command_length = 0;
        execute_command(card);
    }
}

static void receive_write_byte(SdCard *card, u8 value) {
    if (!card->transfer_offset && !card->crc_bytes) {
        if (card->multi_write && value == 0xfd) {
            card->write_wait_token = false;
            card->multi_write = false;
            return;
        }
        if (value != (card->multi_write ? 0xfc : 0xfe))
            return;
        card->transfer_offset = 1;
        return;
    }
    if (card->transfer_offset &&
        card->transfer_offset <= sizeof(card->sector)) {
        card->sector[card->transfer_offset - 1] = value;
        ++card->transfer_offset;
        if (card->transfer_offset > sizeof(card->sector))
            card->crc_bytes = 2;
        return;
    }
    if (card->crc_bytes) {
        --card->crc_bytes;
        if (!card->crc_bytes) {
            bool stored = store_sector(card, card->transfer_lba);

            card->response_delay = 1;
            queue_push(card, stored ? 0x05 : 0x0d);
            queue_push(card, 0x00);
            queue_push(card, 0xff);
            ++card->transfer_lba;
            card->transfer_offset = 0;
            if (!card->multi_write || !stored) {
                card->write_wait_token = false;
                card->multi_write = false;
            }
        }
    }
}

void sd_card_init(SdCard *card) {
    if (!card)
        return;
    memset(card, 0, sizeof(*card));
    card->idle = true;
}

void sd_card_destroy(SdCard *card) {
    if (!card)
        return;
    if (sd_card_eject(card) != 0 && card->image) {
        fclose(card->image);
        card->image = NULL;
    }
    memset(card, 0, sizeof(*card));
}

void sd_card_reset(SdCard *card) {
    if (!card)
        return;
    card->selected = false;
    card->idle = true;
    card->high_capacity = false;
    card->app_command = false;
    card->command_length = 0;
    card->response_delay = 0;
    card->multi_read = false;
    card->write_wait_token = false;
    card->multi_write = false;
    card->transfer_offset = 0;
    card->crc_bytes = 0;
    queue_clear(card);
}

int sd_card_mount(SdCard *card, const char *path, SdImageMode mode) {
    FILE *image;
    SdOffset size;

    if (!card || !path || !path[0] ||
        (mode != SD_IMAGE_READ_ONLY &&
         mode != SD_IMAGE_READ_WRITE)) {
        if (card)
            sd_host_error(card, false,
                          "Invalid SD image or access mode");
        return -1;
    }
    image = fopen(path, mode == SD_IMAGE_READ_WRITE ? "r+b" : "rb");
    if (!image) {
        sd_host_error(card, false,
                      "Cannot open SD image %s for %s access: %s",
                      path,
                      mode == SD_IMAGE_READ_WRITE
                      ? "read/write" : "read-only",
                      sd_system_error());
        return -1;
    }
    if (SD_FSEEK(image, 0, SEEK_END) != 0 ||
        (size = SD_FTELL(image)) <= 0 ||
        (u64)size % SD_CARD_SECTOR_SIZE != 0) {
        fclose(image);
        sd_host_error(card, false,
                      "SD image size must be a non-zero multiple of 512");
        return -1;
    }
    if (card->image && sd_card_eject(card) != 0) {
        fclose(image);
        return -1;
    }
    card->image = image;
    card->sector_count = (u64)size / SD_CARD_SECTOR_SIZE;
    card->image_mode = mode;
    card->dirty = false;
    card->io_error = false;
    card->activity = false;
    card->media_changed = true;
    card->host_error[0] = '\0';
    sd_card_reset(card);
    return 0;
}

int sd_card_flush(SdCard *card) {
    if (!card)
        return -1;
    if (!card->image || !card->dirty)
        return 0;
    clearerr(card->image);
    if (fflush(card->image) != 0 ||
        SD_SYNC(SD_FILENO(card->image)) != 0) {
        sd_host_error(card, true, "SD image flush failed: %s",
                      sd_system_error());
        return -1;
    }
    card->dirty = false;
    return 0;
}

int sd_card_eject(SdCard *card) {
    int result = 0;

    if (!card)
        return -1;
    if (!card->image)
        return 0;
    if (sd_card_flush(card) != 0)
        return -1;
    if (fclose(card->image) != 0) {
        sd_host_error(card, true, "SD image close failed: %s",
                      sd_system_error());
        result = -1;
    }
    card->image = NULL;
    card->sector_count = 0;
    card->dirty = false;
    card->activity = false;
    card->media_changed = true;
    sd_card_reset(card);
    return result;
}

bool sd_card_mounted(const SdCard *card) {
    return card && card->image;
}

bool sd_card_writable(const SdCard *card) {
    return sd_card_mounted(card) &&
           card->image_mode == SD_IMAGE_READ_WRITE;
}

bool sd_card_dirty(const SdCard *card) {
    return card && card->dirty;
}

bool sd_card_has_error(const SdCard *card) {
    return card && card->io_error;
}

const char *sd_card_error(const SdCard *card) {
    return card ? card->host_error : "";
}

bool sd_card_take_activity(SdCard *card) {
    bool activity;

    if (!card)
        return false;
    activity = card->activity;
    card->activity = false;
    return activity;
}

void sd_card_select(SdCard *card, bool selected) {
    if (!card || card->selected == selected)
        return;
    card->selected = selected;
    card->command_length = 0;
}

void sd_card_force_high_capacity(SdCard *card, bool enabled) {
    if (card)
        card->force_high_capacity = enabled;
}

u8 sd_card_transfer(SdCard *card, u8 value) {
    u8 result;

    if (!card || !card->selected || !card->image)
        return 0xff;
    if (!card->response_delay && !card->response_count &&
        card->multi_read) {
        if ((u64)card->transfer_lba < card->sector_count &&
            load_sector(card, card->transfer_lba)) {
            queue_data_block(card, card->sector, sizeof(card->sector));
            ++card->transfer_lba;
        } else {
            card->multi_read = false;
        }
    }
    if (card->response_delay) {
        --card->response_delay;
        result = 0xff;
    } else {
        result = queue_pop(card);
    }
    if (card->write_wait_token && !card->response_count)
        receive_write_byte(card, value);
    else
        parse_command_byte(card, value);
    return result;
}
