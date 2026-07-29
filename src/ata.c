#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "ata.h"

#include <string.h>

#ifdef _WIN32
#define ATA_FSEEK _fseeki64
#define ATA_FTELL _ftelli64
typedef __int64 AtaOffset;
#else
#include <sys/types.h>
#define ATA_FSEEK fseeko
#define ATA_FTELL ftello
typedef off_t AtaOffset;
#endif

static void ata_signature(AtaDevice *ata) {
    ata->error = 0x01;
    ata->sector_count = 0x01;
    ata->lba_low = 0x01;
    ata->lba_mid = 0x00;
    ata->lba_high = 0x00;
    ata->device = 0x00;
}

static void ata_set_error(AtaDevice *ata, u8 error) {
    ata->error = error;
    ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_ERR;
    ata->transfer_read = false;
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
    if (ATA_FSEEK(ata->image, offset, SEEK_SET) != 0 ||
        fread(ata->sector, 1, ATA_SECTOR_SIZE, ata->image) !=
            ATA_SECTOR_SIZE) {
        ata_set_error(ata, ATA_ERROR_UNC);
        return false;
    }
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
}

static void ata_execute(AtaDevice *ata, u8 command) {
    ata->command = command;
    ata->error = 0;
    ata->status &= (u8)~(ATA_STATUS_DRQ | ATA_STATUS_ERR);
    ata->transfer_read = false;
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
            /* This checkpoint mounts raw images read-only. */
            ata_set_error(ata, ATA_ERROR_ABRT);
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
    ata_unmount(ata);
    memset(ata, 0, sizeof(*ata));
}

void ata_reset(AtaDevice *ata) {
    FILE *image;
    u64 sectors;

    if (!ata)
        return;
    image = ata->image;
    sectors = ata->sector_count_total;
    memset(ata, 0, sizeof(*ata));
    ata->image = image;
    ata->sector_count_total = sectors;
    ata_signature(ata);
    if (image)
        ata->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
}

int ata_mount(AtaDevice *ata, const char *path) {
    FILE *image;
    AtaOffset size;

    if (!ata || !path || !path[0])
        return -1;
    image = fopen(path, "rb");
    if (!image)
        return -1;
    if (ATA_FSEEK(image, 0, SEEK_END) != 0 ||
        (size = ATA_FTELL(image)) < (AtaOffset)ATA_SECTOR_SIZE ||
        size % ATA_SECTOR_SIZE != 0 ||
        ATA_FSEEK(image, 0, SEEK_SET) != 0) {
        fclose(image);
        return -1;
    }
    if (ata->image)
        fclose(ata->image);
    ata->image = image;
    ata->sector_count_total = (u64)size / ATA_SECTOR_SIZE;
    ata_reset(ata);
    return 0;
}

void ata_unmount(AtaDevice *ata) {
    if (!ata)
        return;
    if (ata->image)
        fclose(ata->image);
    ata->image = NULL;
    ata->sector_count_total = 0;
    ata_reset(ata);
}

bool ata_is_mounted(const AtaDevice *ata) {
    return ata && ata->image;
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
    (void)ata;
    (void)value;
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
