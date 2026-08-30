#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "scsi_disk.h"

#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define SCSI_FSEEK _fseeki64
#define SCSI_FTELL _ftelli64
#define SCSI_FILENO _fileno
#define SCSI_SYNC _commit
typedef __int64 ScsiOffset;
#else
#include <sys/types.h>
#include <unistd.h>
#define SCSI_FSEEK fseeko
#define SCSI_FTELL ftello
#define SCSI_FILENO fileno
#define SCSI_SYNC fsync
typedef off_t ScsiOffset;
#endif

static bool scsi_trace_enabled(void) {
    const char *value = getenv("MSX_SCSI_TRACE");

    return value && value[0] && strcmp(value, "0") != 0;
}

#define SCSI_STATUS_GOOD            0x00u
#define SCSI_STATUS_CHECK_CONDITION 0x02u

#define SCSI_SENSE_NO_SENSE         0x00u
#define SCSI_SENSE_NOT_READY        0x02u
#define SCSI_SENSE_MEDIUM_ERROR     0x03u
#define SCSI_SENSE_ILLEGAL_REQUEST  0x05u
#define SCSI_SENSE_DATA_PROTECT     0x07u

static const char *scsi_system_error(void) {
    return errno ? strerror(errno) : "I/O operation did not complete";
}

static void scsi_host_error(ScsiDisk *disk, bool device_error,
                            const char *format, ...) {
    va_list arguments;

    if (!disk)
        return;
    disk->io_error |= device_error;
    va_start(arguments, format);
    vsnprintf(disk->host_error, sizeof(disk->host_error),
              format, arguments);
    va_end(arguments);
}

static void scsi_clear_host_error(ScsiDisk *disk) {
    if (disk)
        disk->host_error[0] = '\0';
}

static void scsi_clear_transfer(ScsiDisk *disk) {
    disk->buffer_length = 0;
    disk->buffer_offset = 0;
    disk->transfer_lba = 0;
    disk->blocks_remaining = 0;
    disk->data_out_remaining = 0;
    disk->transfer_read = false;
    disk->transfer_write = false;
    disk->discard_data_out = false;
}

static void scsi_set_sense(ScsiDisk *disk, u8 key, u8 asc, u8 ascq) {
    disk->sense_key = key;
    disk->sense_asc = asc;
    disk->sense_ascq = ascq;
}

static void scsi_status(ScsiDisk *disk, u8 status) {
    disk->status = status;
    disk->phase = SCSI_PHASE_STATUS;
    scsi_clear_transfer(disk);
}

static void scsi_good(ScsiDisk *disk) {
    scsi_status(disk, SCSI_STATUS_GOOD);
}

static void scsi_check(ScsiDisk *disk, u8 key, u8 asc, u8 ascq) {
    scsi_set_sense(disk, key, asc, ascq);
    scsi_status(disk, SCSI_STATUS_CHECK_CONDITION);
}

static void scsi_data_in(ScsiDisk *disk, size_t length) {
    disk->buffer_length = length;
    disk->buffer_offset = 0;
    disk->phase = length ? SCSI_PHASE_DATA_IN : SCSI_PHASE_STATUS;
    if (!length)
        disk->status = SCSI_STATUS_GOOD;
}

static void write_be32(u8 *destination, u32 value) {
    destination[0] = (u8)(value >> 24);
    destination[1] = (u8)(value >> 16);
    destination[2] = (u8)(value >> 8);
    destination[3] = (u8)value;
}

static bool scsi_load_sector(ScsiDisk *disk, u64 lba) {
    ScsiOffset offset;

    if (!disk->image || lba >= disk->sector_count) {
        scsi_check(disk, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return false;
    }
    offset = (ScsiOffset)lba * SCSI_DISK_SECTOR_SIZE;
    clearerr(disk->image);
    if (SCSI_FSEEK(disk->image, offset, SEEK_SET) != 0 ||
        fread(disk->buffer, 1, SCSI_DISK_SECTOR_SIZE,
              disk->image) != SCSI_DISK_SECTOR_SIZE) {
        scsi_host_error(
            disk, true, "SCSI image read failed at sector %llu: %s",
            (unsigned long long)lba, scsi_system_error());
        scsi_check(disk, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
        return false;
    }
    disk->activity = true;
    disk->buffer_length = SCSI_DISK_SECTOR_SIZE;
    disk->buffer_offset = 0;
    return true;
}

static bool scsi_store_sector(ScsiDisk *disk, u64 lba) {
    ScsiOffset offset;
    size_t written;

    if (!disk->image || lba >= disk->sector_count) {
        scsi_check(disk, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return false;
    }
    if (disk->image_mode != ATA_IMAGE_READ_WRITE) {
        scsi_check(disk, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
        return false;
    }
    offset = (ScsiOffset)lba * SCSI_DISK_SECTOR_SIZE;
    clearerr(disk->image);
    if (SCSI_FSEEK(disk->image, offset, SEEK_SET) != 0) {
        scsi_host_error(
            disk, true, "SCSI image seek failed at sector %llu: %s",
            (unsigned long long)lba, scsi_system_error());
        scsi_check(disk, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0x02);
        return false;
    }
    written = fwrite(disk->buffer, 1, SCSI_DISK_SECTOR_SIZE,
                     disk->image);
    if (written != SCSI_DISK_SECTOR_SIZE) {
        disk->dirty |= written != 0;
        scsi_host_error(
            disk, true, "SCSI image write failed at sector %llu: %s",
            (unsigned long long)lba, scsi_system_error());
        scsi_check(disk, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0x02);
        return false;
    }
    disk->dirty = true;
    disk->activity = true;
    return true;
}

static size_t scsi_cdb_length(u8 operation) {
    static const size_t lengths[8] = { 6, 10, 10, 0, 16, 12, 0, 0 };
    size_t length = lengths[operation >> 5];

    return length ? length : 6;
}

static bool scsi_media_ready(ScsiDisk *disk) {
    if (disk->image)
        return true;
    scsi_check(disk, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return false;
}

static bool scsi_range_valid(ScsiDisk *disk, u64 lba, u32 blocks) {
    if (!scsi_media_ready(disk))
        return false;
    if (lba >= disk->sector_count ||
        (u64)blocks > disk->sector_count - lba) {
        scsi_check(disk, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return false;
    }
    return true;
}

static void scsi_begin_read(ScsiDisk *disk, u64 lba, u32 blocks) {
    if (!blocks) {
        scsi_good(disk);
        return;
    }
    if (!scsi_range_valid(disk, lba, blocks))
        return;
    disk->transfer_lba = lba;
    disk->blocks_remaining = blocks;
    disk->transfer_read = true;
    disk->phase = SCSI_PHASE_DATA_IN;
    (void)scsi_load_sector(disk, lba);
}

static void scsi_begin_write(ScsiDisk *disk, u64 lba, u32 blocks) {
    if (!blocks) {
        scsi_good(disk);
        return;
    }
    if (!scsi_range_valid(disk, lba, blocks))
        return;
    if (disk->image_mode != ATA_IMAGE_READ_WRITE) {
        scsi_check(disk, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
        return;
    }
    disk->transfer_lba = lba;
    disk->blocks_remaining = blocks;
    disk->transfer_write = true;
    disk->buffer_length = SCSI_DISK_SECTOR_SIZE;
    disk->buffer_offset = 0;
    memset(disk->buffer, 0, sizeof(disk->buffer));
    disk->phase = SCSI_PHASE_DATA_OUT;
}

static void scsi_build_inquiry(ScsiDisk *disk) {
    static const char vendor[8] = { '1', '9', '8', '3', ' ', ' ', ' ', ' ' };
    static const char product[16] =
        { 'V', 'i', 'r', 't', 'u', 'a', 'l', ' ',
          'S', 'C', 'S', 'I', ' ', 'D', 'i', 's' };
    static const char revision[4] = { '0', '.', '1', '0' };
    size_t allocation = disk->cdb[4];
    size_t length;

    memset(disk->buffer, 0, sizeof(disk->buffer));
    disk->buffer[0] = 0x00; /* Direct-access block device. */
    disk->buffer[2] = 0x01; /* SCSI-1. */
    disk->buffer[3] = 0x01;
    disk->buffer[4] = 31;
    memcpy(&disk->buffer[8], vendor, sizeof(vendor));
    memcpy(&disk->buffer[16], product, sizeof(product));
    memcpy(&disk->buffer[32], revision, sizeof(revision));
    length = allocation < 36 ? allocation : 36;
    scsi_data_in(disk, length);
}

static void scsi_build_request_sense(ScsiDisk *disk) {
    size_t allocation = disk->cdb[4];
    size_t length;

    memset(disk->buffer, 0, sizeof(disk->buffer));
    disk->buffer[0] = 0x70;
    disk->buffer[2] = disk->sense_key;
    disk->buffer[7] = 10;
    disk->buffer[12] = disk->sense_asc;
    disk->buffer[13] = disk->sense_ascq;
    length = allocation < 18 ? allocation : 18;
    scsi_data_in(disk, length);
    scsi_set_sense(disk, SCSI_SENSE_NO_SENSE, 0, 0);
}

static void scsi_build_mode_sense(ScsiDisk *disk) {
    size_t allocation = disk->cdb[4];
    size_t length;
    u32 blocks = disk->sector_count > 0xffffffu
               ? 0xffffffu : (u32)disk->sector_count;

    if (!scsi_media_ready(disk))
        return;
    memset(disk->buffer, 0, sizeof(disk->buffer));
    disk->buffer[0] = 11;
    disk->buffer[2] = disk->image_mode == ATA_IMAGE_READ_WRITE
                    ? 0x00 : 0x80;
    disk->buffer[3] = 8;
    disk->buffer[5] = (u8)(blocks >> 16);
    disk->buffer[6] = (u8)(blocks >> 8);
    disk->buffer[7] = (u8)blocks;
    disk->buffer[9] = 0x00;
    disk->buffer[10] = 0x02;
    disk->buffer[11] = 0x00;
    length = allocation < 12 ? allocation : 12;
    scsi_data_in(disk, length);
}

static void scsi_execute(ScsiDisk *disk) {
    u8 operation = disk->cdb[0];
    u64 lba;
    u32 blocks;

    if (scsi_trace_enabled()) {
        size_t index;

        fprintf(stderr, "[scsi-disk] cdb");
        for (index = 0; index < disk->cdb_expected; ++index)
            fprintf(stderr, " %02X", disk->cdb[index]);
        fputc('\n', stderr);
    }

    scsi_clear_transfer(disk);
    disk->status = SCSI_STATUS_GOOD;
    switch (operation) {
        case 0x00: /* TEST UNIT READY */
            if (scsi_media_ready(disk))
                scsi_good(disk);
            break;
        case 0x01: /* REZERO UNIT */
        case 0x0b: /* SEEK(6) */
        case 0x1b: /* START STOP UNIT */
        case 0x1e: /* PREVENT/ALLOW MEDIUM REMOVAL */
        case 0x2b: /* SEEK(10) */
        case 0x2f: /* VERIFY(10) */
            if (scsi_media_ready(disk))
                scsi_good(disk);
            break;
        case 0x03: /* REQUEST SENSE */
            scsi_build_request_sense(disk);
            break;
        case 0x04: /* FORMAT UNIT: the raw image is already block-formatted. */
            if (!scsi_media_ready(disk))
                break;
            if (disk->image_mode != ATA_IMAGE_READ_WRITE)
                scsi_check(disk, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
            else
                scsi_good(disk);
            break;
        case 0x08: /* READ(6) */
            lba = ((u64)(disk->cdb[1] & 0x1f) << 16) |
                  ((u64)disk->cdb[2] << 8) | disk->cdb[3];
            blocks = disk->cdb[4] ? disk->cdb[4] : 256u;
            scsi_begin_read(disk, lba, blocks);
            break;
        case 0x0a: /* WRITE(6) */
            lba = ((u64)(disk->cdb[1] & 0x1f) << 16) |
                  ((u64)disk->cdb[2] << 8) | disk->cdb[3];
            blocks = disk->cdb[4] ? disk->cdb[4] : 256u;
            scsi_begin_write(disk, lba, blocks);
            break;
        case 0x12: /* INQUIRY */
            scsi_build_inquiry(disk);
            break;
        case 0x15: /* MODE SELECT(6) */
            if (!scsi_media_ready(disk))
                break;
            disk->data_out_remaining = disk->cdb[4];
            disk->discard_data_out = disk->data_out_remaining != 0;
            if (disk->data_out_remaining)
                disk->phase = SCSI_PHASE_DATA_OUT;
            else
                scsi_good(disk);
            break;
        case 0x1a: /* MODE SENSE(6) */
            scsi_build_mode_sense(disk);
            break;
        case 0x25: { /* READ CAPACITY(10) */
            u32 last;

            if (!scsi_media_ready(disk))
                break;
            last = disk->sector_count > 0x100000000ULL
                 ? 0xffffffffu : (u32)(disk->sector_count - 1);
            memset(disk->buffer, 0, sizeof(disk->buffer));
            write_be32(&disk->buffer[0], last);
            write_be32(&disk->buffer[4], SCSI_DISK_SECTOR_SIZE);
            scsi_data_in(disk, 8);
            break;
        }
        case 0x28: /* READ(10) */
            lba = ((u64)disk->cdb[2] << 24) |
                  ((u64)disk->cdb[3] << 16) |
                  ((u64)disk->cdb[4] << 8) | disk->cdb[5];
            blocks = ((u32)disk->cdb[7] << 8) | disk->cdb[8];
            scsi_begin_read(disk, lba, blocks);
            break;
        case 0x2a: /* WRITE(10) */
            lba = ((u64)disk->cdb[2] << 24) |
                  ((u64)disk->cdb[3] << 16) |
                  ((u64)disk->cdb[4] << 8) | disk->cdb[5];
            blocks = ((u32)disk->cdb[7] << 8) | disk->cdb[8];
            scsi_begin_write(disk, lba, blocks);
            break;
        case 0x35: /* SYNCHRONIZE CACHE */
            if (!scsi_media_ready(disk))
                break;
            if (scsi_disk_flush(disk) == 0)
                scsi_good(disk);
            else
                scsi_check(disk, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0x02);
            break;
        default:
            scsi_check(disk, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            break;
    }
}

void scsi_disk_init(ScsiDisk *disk) {
    if (!disk)
        return;
    memset(disk, 0, sizeof(*disk));
    disk->phase = SCSI_PHASE_BUS_FREE;
    disk->image_mode = ATA_IMAGE_READ_ONLY;
}

void scsi_disk_destroy(ScsiDisk *disk) {
    if (!disk)
        return;
    if (scsi_disk_unmount(disk) != 0 && disk->image) {
        fclose(disk->image);
        disk->image = NULL;
    }
    memset(disk, 0, sizeof(*disk));
    disk->phase = SCSI_PHASE_BUS_FREE;
}

void scsi_disk_bus_reset(ScsiDisk *disk) {
    if (!disk)
        return;
    disk->phase = SCSI_PHASE_BUS_FREE;
    disk->cdb_length = 0;
    disk->cdb_expected = 0;
    disk->status = SCSI_STATUS_GOOD;
    disk->message = 0;
    scsi_clear_transfer(disk);
}

int scsi_disk_mount(ScsiDisk *disk, const char *path,
                    AtaImageMode mode) {
    FILE *image;
    ScsiOffset size;

    if (!disk || !path || !path[0] ||
        (mode != ATA_IMAGE_READ_ONLY && mode != ATA_IMAGE_READ_WRITE)) {
        if (disk)
            scsi_host_error(disk, false,
                            "Invalid SCSI image or access mode");
        return -1;
    }
    image = fopen(path, mode == ATA_IMAGE_READ_WRITE ? "r+b" : "rb");
    if (!image) {
        scsi_host_error(
            disk, false, "Cannot open SCSI image %s for %s access: %s",
            path, mode == ATA_IMAGE_READ_WRITE
                  ? "read/write" : "read-only", scsi_system_error());
        return -1;
    }
    errno = 0;
    if (SCSI_FSEEK(image, 0, SEEK_END) != 0 ||
        (size = SCSI_FTELL(image)) < (ScsiOffset)SCSI_DISK_SECTOR_SIZE ||
        size % SCSI_DISK_SECTOR_SIZE != 0 ||
        SCSI_FSEEK(image, 0, SEEK_SET) != 0) {
        int saved_errno = errno;

        fclose(image);
        scsi_host_error(
            disk, false,
            "SCSI image must be a non-empty multiple of 512 bytes%s%s",
            saved_errno ? ": " : "",
            saved_errno ? strerror(saved_errno) : "");
        return -1;
    }
    if (disk->image && scsi_disk_unmount(disk) != 0) {
        fclose(image);
        return -1;
    }
    disk->image = image;
    disk->sector_count = (u64)size / SCSI_DISK_SECTOR_SIZE;
    disk->image_mode = mode;
    disk->dirty = false;
    disk->io_error = false;
    disk->activity = false;
    scsi_clear_host_error(disk);
    scsi_disk_bus_reset(disk);
    scsi_set_sense(disk, SCSI_SENSE_NO_SENSE, 0, 0);
    return 0;
}

int scsi_disk_flush(ScsiDisk *disk) {
    if (!disk || !disk->image || !disk->dirty)
        return 0;
    clearerr(disk->image);
    if (fflush(disk->image) != 0 ||
        SCSI_SYNC(SCSI_FILENO(disk->image)) != 0) {
        scsi_host_error(disk, true, "Could not flush SCSI image: %s",
                        scsi_system_error());
        return -1;
    }
    disk->dirty = false;
    return 0;
}

int scsi_disk_unmount(ScsiDisk *disk) {
    int result = 0;

    if (!disk)
        return -1;
    if (disk->image && scsi_disk_flush(disk) != 0)
        return -1;
    if (disk->image && fclose(disk->image) != 0) {
        scsi_host_error(disk, true, "Could not close SCSI image: %s",
                        scsi_system_error());
        result = -1;
    }
    disk->image = NULL;
    disk->sector_count = 0;
    disk->image_mode = ATA_IMAGE_READ_ONLY;
    disk->dirty = false;
    disk->io_error = false;
    disk->activity = false;
    scsi_disk_bus_reset(disk);
    if (result == 0)
        scsi_clear_host_error(disk);
    return result;
}

bool scsi_disk_mounted(const ScsiDisk *disk) {
    return disk && disk->image;
}

bool scsi_disk_writable(const ScsiDisk *disk) {
    return disk && disk->image &&
           disk->image_mode == ATA_IMAGE_READ_WRITE;
}

bool scsi_disk_dirty(const ScsiDisk *disk) {
    return disk && disk->image && disk->dirty;
}

bool scsi_disk_has_error(const ScsiDisk *disk) {
    return disk && disk->io_error;
}

const char *scsi_disk_error(const ScsiDisk *disk) {
    return disk && disk->host_error[0] ? disk->host_error : "";
}

bool scsi_disk_take_activity(ScsiDisk *disk) {
    bool activity;

    if (!disk)
        return false;
    activity = disk->activity;
    disk->activity = false;
    return activity;
}

void scsi_disk_select(ScsiDisk *disk, bool attention) {
    if (!disk)
        return;
    disk->cdb_length = 0;
    disk->cdb_expected = 0;
    disk->message = 0;
    scsi_clear_transfer(disk);
    disk->phase = attention ? SCSI_PHASE_MESSAGE_OUT
                            : SCSI_PHASE_COMMAND;
}

ScsiPhase scsi_disk_phase(const ScsiDisk *disk) {
    return disk ? disk->phase : SCSI_PHASE_BUS_FREE;
}

bool scsi_disk_phase_is_input(ScsiPhase phase) {
    return phase == SCSI_PHASE_DATA_IN ||
           phase == SCSI_PHASE_STATUS ||
           phase == SCSI_PHASE_MESSAGE_IN;
}

bool scsi_disk_phase_is_output(ScsiPhase phase) {
    return phase == SCSI_PHASE_DATA_OUT ||
           phase == SCSI_PHASE_COMMAND ||
           phase == SCSI_PHASE_MESSAGE_OUT;
}

u8 scsi_disk_current_byte(const ScsiDisk *disk) {
    if (!disk)
        return 0xff;
    switch (disk->phase) {
        case SCSI_PHASE_DATA_IN:
            return disk->buffer_offset < disk->buffer_length
                 ? disk->buffer[disk->buffer_offset] : 0;
        case SCSI_PHASE_STATUS:
            return disk->status;
        case SCSI_PHASE_MESSAGE_IN:
            return disk->message;
        default:
            return 0xff;
    }
}

void scsi_disk_accept_byte(ScsiDisk *disk, u8 value) {
    if (!disk)
        return;
    switch (disk->phase) {
        case SCSI_PHASE_MESSAGE_OUT:
            /* IDENTIFY and NO-OP messages need no further handling for a
             * single non-disconnecting hard-disk target. */
            disk->phase = SCSI_PHASE_COMMAND;
            break;
        case SCSI_PHASE_COMMAND:
            if (disk->cdb_length < sizeof(disk->cdb))
                disk->cdb[disk->cdb_length++] = value;
            if (disk->cdb_length == 1)
                disk->cdb_expected = scsi_cdb_length(value);
            if (disk->cdb_expected &&
                disk->cdb_length >= disk->cdb_expected)
                scsi_execute(disk);
            break;
        case SCSI_PHASE_DATA_OUT:
            if (disk->transfer_write) {
                if (disk->buffer_offset < sizeof(disk->buffer))
                    disk->buffer[disk->buffer_offset++] = value;
                if (disk->buffer_offset == SCSI_DISK_SECTOR_SIZE) {
                    if (!scsi_store_sector(disk, disk->transfer_lba))
                        break;
                    ++disk->transfer_lba;
                    --disk->blocks_remaining;
                    disk->buffer_offset = 0;
                    if (!disk->blocks_remaining)
                        scsi_good(disk);
                }
            } else if (disk->discard_data_out) {
                if (disk->data_out_remaining)
                    --disk->data_out_remaining;
                if (!disk->data_out_remaining)
                    scsi_good(disk);
            }
            break;
        default:
            break;
    }
}

void scsi_disk_advance_byte(ScsiDisk *disk) {
    if (!disk)
        return;
    switch (disk->phase) {
        case SCSI_PHASE_DATA_IN:
            if (disk->buffer_offset < disk->buffer_length)
                ++disk->buffer_offset;
            if (disk->buffer_offset < disk->buffer_length)
                break;
            if (disk->transfer_read) {
                --disk->blocks_remaining;
                ++disk->transfer_lba;
                if (disk->blocks_remaining) {
                    (void)scsi_load_sector(disk, disk->transfer_lba);
                    break;
                }
                disk->transfer_read = false;
            }
            scsi_good(disk);
            break;
        case SCSI_PHASE_STATUS:
            disk->phase = SCSI_PHASE_MESSAGE_IN;
            disk->message = 0x00; /* COMMAND COMPLETE */
            break;
        case SCSI_PHASE_MESSAGE_IN:
            disk->phase = SCSI_PHASE_BUS_FREE;
            break;
        default:
            break;
    }
}
