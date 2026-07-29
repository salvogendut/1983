#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "ata.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define ATA_FSEEK _fseeki64
#define ATA_FTELL _ftelli64
#define ATA_FILENO _fileno
#define ATA_SYNC _commit
typedef __int64 AtaOffset;
#else
#include <sys/types.h>
#include <unistd.h>
#define ATA_FSEEK fseeko
#define ATA_FTELL ftello
#define ATA_FILENO fileno
#define ATA_SYNC fsync
typedef off_t AtaOffset;
#endif

static const char *ata_system_error(void) {
    return errno ? strerror(errno) : "I/O operation did not complete";
}

static void ata_signature(AtaDevice *ata) {
    ata->error = 0x01;
    ata->sector_count = 0x01;
    ata->lba_low = 0x01;
    ata->lba_mid = 0x00;
    ata->lba_high = 0x00;
    ata->device = 0x00;
}

static void ata_host_error(AtaDevice *ata, bool device_error,
                           const char *format, ...) {
    va_list arguments;

    if (!ata)
        return;
    ata->io_error |= device_error;
    va_start(arguments, format);
    vsnprintf(ata->host_error, sizeof(ata->host_error),
              format, arguments);
    va_end(arguments);
}

static void ata_clear_host_error(AtaDevice *ata) {
    if (!ata)
        return;
    ata->host_error[0] = '\0';
}

static void ata_set_error(AtaDevice *ata, u8 error) {
    ata->error = error;
    ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_ERR;
    ata->transfer_read = false;
    ata->transfer_write = false;
    ata->transfer_offset = 0;
    ata->sectors_left = 0;
}

static u32 ata_lba(const AtaDevice *ata) {
    return (u32)ata->lba_low |
           ((u32)ata->lba_mid << 8) |
           ((u32)ata->lba_high << 16) |
           ((u32)(ata->device & 0x0f) << 24);
}

static unsigned ata_requested_sectors(const AtaDevice *ata) {
    return ata->sector_count ? ata->sector_count : 256u;
}

static bool ata_load_sector(AtaDevice *ata, u32 lba) {
    AtaOffset offset;

    if (!ata->image || (u64)lba >= ata->sector_count_total) {
        ata_set_error(ata, ATA_ERROR_IDNF);
        return false;
    }
    offset = (AtaOffset)lba * ATA_SECTOR_SIZE;
    clearerr(ata->image);
    if (ATA_FSEEK(ata->image, offset, SEEK_SET) != 0 ||
        fread(ata->sector, 1, ATA_SECTOR_SIZE, ata->image) !=
            ATA_SECTOR_SIZE) {
        ata_host_error(
            ata, true, "IDE image read failed at sector %u: %s",
            lba, ata_system_error());
        ata_set_error(ata, ATA_ERROR_UNC);
        return false;
    }
    ata->activity = true;
    return true;
}

static bool ata_store_sector(AtaDevice *ata, u32 lba) {
    AtaOffset offset;
    size_t written;

    if (!ata->image || ata->image_mode != ATA_IMAGE_READ_WRITE ||
        (u64)lba >= ata->sector_count_total) {
        ata_set_error(
            ata, ata->image_mode == ATA_IMAGE_READ_WRITE
                 ? ATA_ERROR_IDNF : ATA_ERROR_ABRT);
        return false;
    }
    offset = (AtaOffset)lba * ATA_SECTOR_SIZE;
    clearerr(ata->image);
    if (ATA_FSEEK(ata->image, offset, SEEK_SET) != 0) {
        ata_host_error(
            ata, true, "IDE image seek failed at sector %u: %s",
            lba, ata_system_error());
        ata_set_error(ata, ATA_ERROR_UNC);
        return false;
    }
    written = fwrite(ata->sector, 1, ATA_SECTOR_SIZE, ata->image);
    if (written != ATA_SECTOR_SIZE) {
        ata->dirty |= written != 0;
        ata_host_error(
            ata, true, "IDE image write failed at sector %u: %s",
            lba, ata_system_error());
        ata_set_error(ata, ATA_ERROR_UNC);
        return false;
    }
    ata->dirty = true;
    ata->activity = true;
    return true;
}

static void ata_write_word(u8 *data, unsigned word, u16 value) {
    data[word * 2] = (u8)value;
    data[word * 2 + 1] = (u8)(value >> 8);
}

static void ata_write_string(u8 *data, unsigned first_word,
                             unsigned words, const char *text) {
    size_t length = text ? strlen(text) : 0;

    for (unsigned i = 0; i < words; ++i) {
        size_t offset = i * 2;
        u8 first = offset < length ? (u8)text[offset] : (u8)' ';
        u8 second =
            offset + 1 < length ? (u8)text[offset + 1] : (u8)' ';

        data[(first_word + i) * 2] = second;
        data[(first_word + i) * 2 + 1] = first;
    }
}

static void ata_build_identify(AtaDevice *ata) {
    u32 sectors =
        ata->sector_count_total > 0x0fffffffu
        ? 0x0fffffffu : (u32)ata->sector_count_total;
    u32 cylinders = sectors / (16u * 63u);
    u32 chs_capacity;

    if (cylinders < 1)
        cylinders = 1;
    if (cylinders > 65535u)
        cylinders = 65535u;
    chs_capacity = cylinders * 16u * 63u;

    memset(ata->sector, 0, sizeof(ata->sector));
    ata_write_word(ata->sector, 0, 0x0040);
    ata_write_word(ata->sector, 1, (u16)cylinders);
    ata_write_word(ata->sector, 3, 16);
    ata_write_word(ata->sector, 6, 63);
    ata_write_string(ata->sector, 10, 10, "1983SUNRISE00000001");
    ata_write_string(ata->sector, 23, 4, "1.0");
    ata_write_string(ata->sector, 27, 20, "1983 Sunrise IDE disk");
    ata_write_word(ata->sector, 47, 0x8010);
    ata_write_word(ata->sector, 49, 0x0200);
    ata_write_word(ata->sector, 53, 0x0001);
    ata_write_word(ata->sector, 54, (u16)cylinders);
    ata_write_word(ata->sector, 55, 16);
    ata_write_word(ata->sector, 56, 63);
    ata_write_word(ata->sector, 57, (u16)chs_capacity);
    ata_write_word(ata->sector, 58, (u16)(chs_capacity >> 16));
    ata_write_word(ata->sector, 60, (u16)sectors);
    ata_write_word(ata->sector, 61, (u16)(sectors >> 16));
    ata_write_word(ata->sector, 80, 0x0006);
    ata_write_word(ata->sector, 82, 0x1000);
    ata_write_word(ata->sector, 85, 0x1000);
}

static void ata_execute(AtaDevice *ata, u8 command) {
    ata->command = command;
    ata->error = 0;
    ata->status &= (u8)~(ATA_STATUS_DRQ | ATA_STATUS_ERR);
    ata->transfer_read = false;
    ata->transfer_write = false;
    ata->transfer_offset = 0;
    ata->sectors_left = 0;

    switch (command) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
        case 0x91:
            ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
            break;
        case 0x20:
        case 0x21:
            ata->transfer_lba = ata_lba(ata);
            ata->sectors_left = ata_requested_sectors(ata);
            if ((u64)ata->transfer_lba + ata->sectors_left >
                ata->sector_count_total) {
                ata_set_error(ata, ATA_ERROR_IDNF);
            } else if (ata_load_sector(ata, ata->transfer_lba)) {
                ata->transfer_read = true;
                ata->status =
                    ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_DRQ;
            }
            break;
        case 0x30:
        case 0x31:
            ata->transfer_lba = ata_lba(ata);
            ata->sectors_left = ata_requested_sectors(ata);
            if (ata->image_mode != ATA_IMAGE_READ_WRITE) {
                ata_set_error(ata, ATA_ERROR_ABRT);
            } else if ((u64)ata->transfer_lba + ata->sectors_left >
                       ata->sector_count_total) {
                ata_set_error(ata, ATA_ERROR_IDNF);
            } else {
                memset(ata->sector, 0, sizeof(ata->sector));
                ata->transfer_write = true;
                ata->status =
                    ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_DRQ;
            }
            break;
        case 0xe7:
        case 0xea:
            if (ata_flush(ata) != 0)
                ata_set_error(ata, ATA_ERROR_UNC);
            else
                ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
            break;
        case 0x90:
            ata_signature(ata);
            ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
            break;
        case 0xec:
            ata_build_identify(ata);
            ata->transfer_read = true;
            ata->transfer_offset = 0;
            ata->sectors_left = 0;
            ata->status =
                ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_DRQ;
            break;
        case 0xef:
            if (ata->features == 0x03)
                ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
            else
                ata_set_error(ata, ATA_ERROR_ABRT);
            break;
        case 0xf8: {
            u32 maximum =
                ata->sector_count_total
                ? (u32)(ata->sector_count_total - 1) : 0;

            ata->lba_low = (u8)maximum;
            ata->lba_mid = (u8)(maximum >> 8);
            ata->lba_high = (u8)(maximum >> 16);
            ata->device =
                (ata->device & 0xf0) | (u8)(maximum >> 24);
            ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
            break;
        }
        default:
            ata_set_error(ata, ATA_ERROR_ABRT);
            break;
    }
}

void ata_init(AtaDevice *ata) {
    if (!ata)
        return;
    memset(ata, 0, sizeof(*ata));
    ata_signature(ata);
}

void ata_destroy(AtaDevice *ata) {
    if (!ata)
        return;
    if (ata_unmount(ata) != 0 && ata->image) {
        fclose(ata->image);
        ata->image = NULL;
    }
    memset(ata, 0, sizeof(*ata));
}

void ata_reset(AtaDevice *ata) {
    FILE *image;
    u64 sectors;
    AtaImageMode mode;
    bool dirty;
    bool io_error;
    char host_error[sizeof(ata->host_error)];

    if (!ata)
        return;
    image = ata->image;
    sectors = ata->sector_count_total;
    mode = ata->image_mode;
    dirty = ata->dirty;
    io_error = ata->io_error;
    memcpy(host_error, ata->host_error, sizeof(host_error));
    memset(ata, 0, sizeof(*ata));
    ata->image = image;
    ata->sector_count_total = sectors;
    ata->image_mode = mode;
    ata->dirty = dirty;
    ata->io_error = io_error;
    memcpy(ata->host_error, host_error, sizeof(ata->host_error));
    ata_signature(ata);
    if (image)
        ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
}

int ata_mount(AtaDevice *ata, const char *path) {
    return ata_mount_mode(ata, path, ATA_IMAGE_READ_ONLY);
}

int ata_mount_mode(AtaDevice *ata, const char *path,
                   AtaImageMode mode) {
    FILE *image;
    AtaOffset size;

    if (!ata)
        return -1;
    if (!path || !path[0] ||
        (mode != ATA_IMAGE_READ_ONLY &&
         mode != ATA_IMAGE_READ_WRITE)) {
        ata_host_error(ata, false, "Invalid IDE image or access mode");
        return -1;
    }
    image = fopen(path, mode == ATA_IMAGE_READ_WRITE ? "r+b" : "rb");
    if (!image) {
        ata_host_error(
            ata, false, "Cannot open IDE image %s for %s access: %s",
            path,
            mode == ATA_IMAGE_READ_WRITE ? "read/write" : "read-only",
            ata_system_error());
        return -1;
    }
    errno = 0;
    if (ATA_FSEEK(image, 0, SEEK_END) != 0 ||
        (size = ATA_FTELL(image)) < (AtaOffset)ATA_SECTOR_SIZE ||
        size % ATA_SECTOR_SIZE != 0 ||
        ATA_FSEEK(image, 0, SEEK_SET) != 0) {
        int saved_errno = errno;

        fclose(image);
        ata_host_error(
            ata, false,
            "IDE image must be a non-empty multiple of 512 bytes%s%s",
            saved_errno ? ": " : "",
            saved_errno ? strerror(saved_errno) : "");
        return -1;
    }
    if (ata->image && ata_unmount(ata) != 0) {
        fclose(image);
        return -1;
    }
    ata->image = image;
    ata->sector_count_total = (u64)size / ATA_SECTOR_SIZE;
    ata->image_mode = mode;
    ata->dirty = false;
    ata->io_error = false;
    ata_clear_host_error(ata);
    ata_reset(ata);
    return 0;
}

int ata_flush(AtaDevice *ata) {
    if (!ata || !ata->image || !ata->dirty)
        return 0;
    clearerr(ata->image);
    if (fflush(ata->image) != 0 ||
        ATA_SYNC(ATA_FILENO(ata->image)) != 0) {
        ata_host_error(
            ata, true, "Could not flush IDE image: %s",
            ata_system_error());
        return -1;
    }
    ata->dirty = false;
    return 0;
}

int ata_unmount(AtaDevice *ata) {
    int result = 0;

    if (!ata)
        return -1;
    if (ata->image && ata_flush(ata) != 0)
        return -1;
    if (ata->image && fclose(ata->image) != 0) {
        ata_host_error(
            ata, true, "Could not close IDE image: %s",
            ata_system_error());
        result = -1;
    }
    ata->image = NULL;
    ata->sector_count_total = 0;
    ata->image_mode = ATA_IMAGE_READ_ONLY;
    ata->dirty = false;
    ata->io_error = false;
    ata_reset(ata);
    if (result == 0)
        ata_clear_host_error(ata);
    return result;
}

bool ata_is_mounted(const AtaDevice *ata) {
    return ata && ata->image;
}

bool ata_is_writable(const AtaDevice *ata) {
    return ata && ata->image &&
           ata->image_mode == ATA_IMAGE_READ_WRITE;
}

bool ata_is_dirty(const AtaDevice *ata) {
    return ata && ata->image && ata->dirty;
}

bool ata_has_io_error(const AtaDevice *ata) {
    return ata && ata->io_error;
}

const char *ata_last_error(const AtaDevice *ata) {
    return ata && ata->host_error[0] ? ata->host_error : "";
}

u64 ata_total_sectors(const AtaDevice *ata) {
    return ata ? ata->sector_count_total : 0;
}

bool ata_take_activity(AtaDevice *ata) {
    bool activity;

    if (!ata)
        return false;
    activity = ata->activity;
    ata->activity = false;
    return activity;
}

u16 ata_read_data(AtaDevice *ata) {
    u16 value;

    if (!ata || !ata->image || !ata->transfer_read ||
        !(ata->status & ATA_STATUS_DRQ) ||
        ata->transfer_offset + 1 >= ATA_SECTOR_SIZE)
        return 0x7f7f;
    value = ata->sector[ata->transfer_offset] |
            ((u16)ata->sector[ata->transfer_offset + 1] << 8);
    ata->transfer_offset += 2;
    if (ata->transfer_offset == ATA_SECTOR_SIZE) {
        if (ata->command == 0xec || --ata->sectors_left == 0) {
            ata->transfer_read = false;
            ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
        } else {
            ++ata->transfer_lba;
            if (ata_load_sector(ata, ata->transfer_lba))
                ata->transfer_offset = 0;
        }
    }
    return value;
}

void ata_write_data(AtaDevice *ata, u16 value) {
    if (!ata || !ata->image || !ata->transfer_write ||
        !(ata->status & ATA_STATUS_DRQ) ||
        ata->transfer_offset + 1 >= ATA_SECTOR_SIZE)
        return;
    ata->sector[ata->transfer_offset] = (u8)value;
    ata->sector[ata->transfer_offset + 1] = (u8)(value >> 8);
    ata->transfer_offset += 2;
    if (ata->transfer_offset != ATA_SECTOR_SIZE)
        return;
    if (!ata_store_sector(ata, ata->transfer_lba))
        return;
    if (--ata->sectors_left == 0) {
        ata->transfer_write = false;
        ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
    } else {
        ++ata->transfer_lba;
        ata->transfer_offset = 0;
        memset(ata->sector, 0, sizeof(ata->sector));
    }
}

u8 ata_read_register(const AtaDevice *ata, unsigned reg) {
    if (!ata || !ata->image)
        return 0x7f;
    switch (reg & 0x0f) {
        case 1: return ata->error;
        case 2: return ata->sector_count;
        case 3: return ata->lba_low;
        case 4: return ata->lba_mid;
        case 5: return ata->lba_high;
        case 6: return ata->device;
        case 7: return ata->status;
        default: return 0x7f;
    }
}

void ata_write_register(AtaDevice *ata, unsigned reg, u8 value) {
    if (!ata || !ata->image)
        return;
    switch (reg & 0x0f) {
        case 1: ata->features = value; break;
        case 2: ata->sector_count = value; break;
        case 3: ata->lba_low = value; break;
        case 4: ata->lba_mid = value; break;
        case 5: ata->lba_high = value; break;
        case 6: ata->device = value; break;
        case 7: ata_execute(ata, value); break;
        default: break;
    }
}
