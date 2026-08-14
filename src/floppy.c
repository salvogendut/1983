#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "floppy.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define FLOPPY_FSEEK _fseeki64
#define FLOPPY_FTELL _ftelli64
#define FLOPPY_FILENO _fileno
#define FLOPPY_SYNC _commit
typedef __int64 FloppyOffset;
#else
#include <sys/types.h>
#include <unistd.h>
#define FLOPPY_FSEEK fseeko
#define FLOPPY_FTELL ftello
#define FLOPPY_FILENO fileno
#define FLOPPY_SYNC fsync
typedef off_t FloppyOffset;
#endif

static const char *floppy_system_error(void) {
    return errno ? strerror(errno) : "I/O operation did not complete";
}

static void floppy_host_error(FloppyImage *image, bool device_error,
                              const char *format, ...) {
    va_list arguments;

    if (!image)
        return;
    image->io_error |= device_error;
    va_start(arguments, format);
    vsnprintf(image->host_error, sizeof(image->host_error),
              format, arguments);
    va_end(arguments);
}

static unsigned read_le16(const u8 *data) {
    return (unsigned)data[0] | ((unsigned)data[1] << 8);
}

static unsigned read_le32(const u8 *data) {
    return (unsigned)data[0] |
           ((unsigned)data[1] << 8) |
           ((unsigned)data[2] << 16) |
           ((unsigned)data[3] << 24);
}

static bool geometry_from_boot_sector(const u8 *boot, u64 sectors,
                                      unsigned *tracks,
                                      unsigned *sides,
                                      unsigned *sectors_per_track) {
    unsigned bytes_per_sector = read_le16(boot + 11);
    unsigned total = read_le16(boot + 19);
    unsigned candidate_spt = read_le16(boot + 24);
    unsigned candidate_sides = read_le16(boot + 26);
    unsigned denominator;

    if (!total)
        total = read_le32(boot + 32);
    if (bytes_per_sector != FLOPPY_SECTOR_SIZE ||
        total != sectors ||
        candidate_spt < 1 || candidate_spt > 32 ||
        candidate_sides < 1 || candidate_sides > 2)
        return false;
    denominator = candidate_spt * candidate_sides;
    if (!denominator || total % denominator)
        return false;
    *tracks = total / denominator;
    if (*tracks < 1 || *tracks > 255)
        return false;
    *sides = candidate_sides;
    *sectors_per_track = candidate_spt;
    return true;
}

static bool geometry_from_size(u64 sectors, unsigned *tracks,
                               unsigned *sides,
                               unsigned *sectors_per_track) {
    struct Geometry {
        u64 sectors;
        unsigned tracks;
        unsigned sides;
        unsigned sectors_per_track;
    };
    static const struct Geometry geometries[] = {
        { 1440, 80, 2, 9 },
        { 1280, 80, 2, 8 },
        {  720, 40, 2, 9 },
        {  640, 40, 2, 8 },
        {  360, 40, 1, 9 },
        {  320, 40, 1, 8 },
    };

    for (size_t i = 0;
         i < sizeof(geometries) / sizeof(geometries[0]); ++i) {
        if (sectors != geometries[i].sectors)
            continue;
        *tracks = geometries[i].tracks;
        *sides = geometries[i].sides;
        *sectors_per_track = geometries[i].sectors_per_track;
        return true;
    }
    return false;
}

static bool inspect_raw_image(FILE *file, FloppyOffset size,
                              unsigned *tracks, unsigned *sides,
                              unsigned *sectors_per_track) {
    u8 boot[FLOPPY_SECTOR_SIZE];
    u64 sectors;

    if (size < (FloppyOffset)FLOPPY_SECTOR_SIZE ||
        size % FLOPPY_SECTOR_SIZE)
        return false;
    sectors = (u64)size / FLOPPY_SECTOR_SIZE;
    if (FLOPPY_FSEEK(file, 0, SEEK_SET) != 0 ||
        fread(boot, 1, sizeof(boot), file) != sizeof(boot))
        return false;
    if (geometry_from_boot_sector(
            boot, sectors, tracks, sides, sectors_per_track))
        return true;
    return geometry_from_size(
        sectors, tracks, sides, sectors_per_track);
}

static void free_track_info(FloppyImage *image) {
    if (!image)
        return;
    free(image->track_info);
    image->track_info = NULL;
}

static bool inspect_cpc_dsk(FILE *file, FloppyOffset size,
                            unsigned *tracks, unsigned *sides,
                            unsigned *sectors_per_track,
                            FloppyTrackInfo **track_info,
                            bool *recognized) {
    static const char standard_magic[] = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    static const char extended_magic[] = "EXTENDED CPC DSK File\r\nDisk-Info\r\n";
    u8 disk_header[256];
    FloppyTrackInfo *info;
    FloppyOffset track_offset = 256;
    unsigned track_count;
    unsigned standard_track_size;
    unsigned maximum_sectors = 0;
    bool extended;

    *recognized = false;
    if (size < (FloppyOffset)sizeof(disk_header) ||
        FLOPPY_FSEEK(file, 0, SEEK_SET) != 0 ||
        fread(disk_header, 1, sizeof(disk_header), file) !=
            sizeof(disk_header))
        return false;
    if (memcmp(disk_header, extended_magic,
               sizeof(extended_magic) - 1) == 0)
        extended = true;
    else if (memcmp(disk_header, standard_magic,
                    sizeof(standard_magic) - 1) == 0)
        extended = false;
    else
        return false;
    *recognized = true;

    *tracks = disk_header[0x30];
    *sides = disk_header[0x31];
    if (*tracks < 1 || *tracks > 255 ||
        *sides < 1 || *sides > 2)
        return false;
    track_count = *tracks * *sides;
    if (extended && track_count > 204)
        return false;
    standard_track_size = read_le16(disk_header + 0x32);
    if (!extended && standard_track_size < 256)
        return false;

    info = calloc(track_count, sizeof(*info));
    if (!info)
        return false;
    for (unsigned index = 0; index < track_count; ++index) {
        u8 header[256];
        unsigned track_size = extended
            ? (unsigned)disk_header[0x34 + index] * 256u
            : standard_track_size;
        unsigned data_offset = 256;
        unsigned sector_count;

        if (!track_size)
            continue;
        if (track_size < sizeof(header) ||
            track_offset > size - (FloppyOffset)track_size ||
            FLOPPY_FSEEK(file, track_offset, SEEK_SET) != 0 ||
            fread(header, 1, sizeof(header), file) != sizeof(header) ||
            memcmp(header, "Track-Info\r\n", 12) != 0) {
            free(info);
            return false;
        }
        sector_count = header[0x15];
        if (sector_count > FLOPPY_MAX_SECTORS_PER_TRACK) {
            free(info);
            return false;
        }
        info[index].sector_count = sector_count;
        if (sector_count > maximum_sectors)
            maximum_sectors = sector_count;
        for (unsigned sector = 0; sector < sector_count; ++sector) {
            const u8 *descriptor = header + 0x18 + sector * 8;
            unsigned sector_size;

            if (descriptor[3] > 6) {
                free(info);
                return false;
            }
            sector_size = extended
                ? read_le16(descriptor + 6)
                : (128u << descriptor[3]);
            if (!sector_size)
                sector_size = 128u << descriptor[3];
            /* The current WD2793 data path models MSX-sized sectors. */
            if (sector_size != FLOPPY_SECTOR_SIZE ||
                data_offset > track_size - sector_size) {
                free(info);
                return false;
            }
            info[index].sectors[sector].id = descriptor[2];
            info[index].sectors[sector].size_code = descriptor[3];
            info[index].sectors[sector].st1 = descriptor[4];
            info[index].sectors[sector].st2 = descriptor[5];
            info[index].sectors[sector].size = sector_size;
            info[index].sectors[sector].offset =
                (u64)track_offset + data_offset;
            data_offset += sector_size;
        }
        track_offset += track_size;
    }
    if (!maximum_sectors) {
        free(info);
        return false;
    }
    *sectors_per_track = maximum_sectors;
    *track_info = info;
    return true;
}

void floppy_image_init(FloppyImage *image) {
    if (!image)
        return;
    memset(image, 0, sizeof(*image));
}

void floppy_image_destroy(FloppyImage *image) {
    if (!image)
        return;
    if (floppy_image_eject(image) != 0 && image->file) {
        fclose(image->file);
        image->file = NULL;
    }
    free_track_info(image);
    memset(image, 0, sizeof(*image));
}

int floppy_image_mount(FloppyImage *image, const char *path,
                       FloppyImageMode mode) {
    FILE *file;
    FloppyOffset size;
    unsigned tracks;
    unsigned sides;
    unsigned sectors_per_track;
    FloppyImageFormat format;
    FloppyTrackInfo *track_info = NULL;
    bool cpc_recognized;

    if (!image || !path || !path[0] ||
        (mode != FLOPPY_IMAGE_READ_ONLY &&
         mode != FLOPPY_IMAGE_READ_WRITE)) {
        floppy_host_error(image, false,
                          "Invalid floppy image or access mode");
        return -1;
    }
    file = fopen(path, mode == FLOPPY_IMAGE_READ_WRITE ? "r+b" : "rb");
    if (!file) {
        floppy_host_error(
            image, false, "Cannot open floppy image for %s access: %s",
            mode == FLOPPY_IMAGE_READ_WRITE
            ? "read/write" : "read-only",
            floppy_system_error());
        return -1;
    }
    errno = 0;
    if (FLOPPY_FSEEK(file, 0, SEEK_END) != 0 ||
        (size = FLOPPY_FTELL(file)) < 0) {
        int saved_errno = errno;

        fclose(file);
        floppy_host_error(
            image, false,
            "Could not inspect floppy image%s%s",
            saved_errno ? ": " : "",
            saved_errno ? strerror(saved_errno) : "");
        return -1;
    }
    if (inspect_cpc_dsk(file, size, &tracks, &sides,
                        &sectors_per_track, &track_info,
                        &cpc_recognized)) {
        format = FLOPPY_FORMAT_CPC_DSK;
    } else if (!cpc_recognized &&
               inspect_raw_image(file, size, &tracks, &sides,
                                 &sectors_per_track)) {
        format = FLOPPY_FORMAT_RAW;
    } else {
        fclose(file);
        floppy_host_error(
            image, false,
            "Unsupported floppy image (expected raw MSX or CPCEMU DSK)");
        return -1;
    }
    if (image->file && floppy_image_eject(image) != 0) {
        fclose(file);
        free(track_info);
        return -1;
    }
    image->file = file;
    image->mode = mode;
    image->format = format;
    image->tracks = tracks;
    image->sides = sides;
    image->sectors_per_track = sectors_per_track;
    image->track_info = track_info;
    image->dirty = false;
    image->activity = false;
    image->disk_changed = true;
    image->io_error = false;
    image->host_error[0] = '\0';
    return 0;
}

int floppy_image_flush(FloppyImage *image) {
    if (!image || !image->file || !image->dirty)
        return 0;
    clearerr(image->file);
    if (fflush(image->file) != 0 ||
        FLOPPY_SYNC(FLOPPY_FILENO(image->file)) != 0) {
        floppy_host_error(
            image, true, "Could not flush floppy image: %s",
            floppy_system_error());
        return -1;
    }
    image->dirty = false;
    return 0;
}

int floppy_image_eject(FloppyImage *image) {
    if (!image)
        return -1;
    if (image->file && floppy_image_flush(image) != 0)
        return -1;
    if (image->file && fclose(image->file) != 0) {
        floppy_host_error(
            image, true, "Could not close floppy image: %s",
            floppy_system_error());
        return -1;
    }
    image->file = NULL;
    image->mode = FLOPPY_IMAGE_READ_ONLY;
    image->format = FLOPPY_FORMAT_RAW;
    image->tracks = 0;
    image->sides = 0;
    image->sectors_per_track = 0;
    free_track_info(image);
    image->dirty = false;
    image->activity = false;
    image->disk_changed = true;
    image->io_error = false;
    image->host_error[0] = '\0';
    return 0;
}

bool floppy_image_mounted(const FloppyImage *image) {
    return image && image->file;
}

bool floppy_image_writable(const FloppyImage *image) {
    return image && image->file &&
           image->mode == FLOPPY_IMAGE_READ_WRITE;
}

bool floppy_image_dirty(const FloppyImage *image) {
    return image && image->dirty;
}

bool floppy_image_has_error(const FloppyImage *image) {
    return image && image->io_error;
}

const char *floppy_image_error(const FloppyImage *image) {
    return image ? image->host_error : "";
}

bool floppy_image_take_activity(FloppyImage *image) {
    bool activity;

    if (!image)
        return false;
    activity = image->activity;
    image->activity = false;
    return activity;
}

bool floppy_image_take_disk_changed(FloppyImage *image) {
    bool changed;

    if (!image)
        return false;
    changed = image->disk_changed;
    image->disk_changed = false;
    return changed;
}

static const FloppySectorInfo *find_sector(const FloppyImage *image,
                                           unsigned track,
                                           unsigned side,
                                           unsigned sector) {
    const FloppyTrackInfo *info;

    if (!image || !image->file || image->format != FLOPPY_FORMAT_CPC_DSK ||
        track >= image->tracks || side >= image->sides ||
        !image->track_info)
        return NULL;
    info = &image->track_info[track * image->sides + side];
    for (unsigned index = 0; index < info->sector_count; ++index) {
        if (info->sectors[index].id == sector)
            return &info->sectors[index];
    }
    return NULL;
}

static bool sector_offset(const FloppyImage *image, unsigned track,
                          unsigned side, unsigned sector,
                          FloppyOffset *offset) {
    u64 logical_sector;
    const FloppySectorInfo *info;

    if (!image || !image->file ||
        track >= image->tracks || side >= image->sides)
        return false;
    if (image->format == FLOPPY_FORMAT_CPC_DSK) {
        info = find_sector(image, track, side, sector);
        if (!info || info->size != FLOPPY_SECTOR_SIZE)
            return false;
        *offset = (FloppyOffset)info->offset;
        return true;
    }
    if (sector < 1 || sector > image->sectors_per_track)
        return false;
    logical_sector =
        ((u64)track * image->sides + side) *
        image->sectors_per_track + (sector - 1);
    *offset = (FloppyOffset)(logical_sector * FLOPPY_SECTOR_SIZE);
    return true;
}

bool floppy_image_first_sector(const FloppyImage *image,
                               unsigned track, unsigned side,
                               unsigned *sector) {
    if (!image || !sector || !image->file ||
        track >= image->tracks || side >= image->sides)
        return false;
    if (image->format == FLOPPY_FORMAT_CPC_DSK) {
        const FloppyTrackInfo *info =
            &image->track_info[track * image->sides + side];

        if (!info->sector_count)
            return false;
        *sector = info->sectors[0].id;
        return true;
    }
    *sector = 1;
    return true;
}

bool floppy_image_next_sector(const FloppyImage *image,
                              unsigned track, unsigned side,
                              unsigned sector, unsigned *next_sector) {
    if (!image || !next_sector || !image->file ||
        track >= image->tracks || side >= image->sides)
        return false;
    if (image->format == FLOPPY_FORMAT_CPC_DSK) {
        const FloppyTrackInfo *info =
            &image->track_info[track * image->sides + side];

        for (unsigned index = 0; index + 1 < info->sector_count; ++index) {
            if (info->sectors[index].id == sector) {
                *next_sector = info->sectors[index + 1].id;
                return true;
            }
        }
        return false;
    }
    if (sector < 1 || sector >= image->sectors_per_track)
        return false;
    *next_sector = sector + 1;
    return true;
}

bool floppy_image_sector_info(const FloppyImage *image,
                              unsigned track, unsigned side,
                              unsigned sector, u8 *size_code,
                              u8 *st1, u8 *st2) {
    const FloppySectorInfo *info;

    if (!image || !image->file || track >= image->tracks ||
        side >= image->sides)
        return false;
    if (image->format == FLOPPY_FORMAT_CPC_DSK) {
        info = find_sector(image, track, side, sector);
        if (!info)
            return false;
        if (size_code)
            *size_code = info->size_code;
        if (st1)
            *st1 = info->st1;
        if (st2)
            *st2 = info->st2;
        return true;
    }
    if (sector < 1 || sector > image->sectors_per_track)
        return false;
    if (size_code)
        *size_code = 2;
    if (st1)
        *st1 = 0;
    if (st2)
        *st2 = 0;
    return true;
}

int floppy_image_read_sector(FloppyImage *image, unsigned track,
                             unsigned side, unsigned sector,
                             u8 data[FLOPPY_SECTOR_SIZE]) {
    FloppyOffset offset;

    if (!data ||
        !sector_offset(image, track, side, sector, &offset))
        return -1;
    clearerr(image->file);
    if (FLOPPY_FSEEK(image->file, offset, SEEK_SET) != 0 ||
        fread(data, 1, FLOPPY_SECTOR_SIZE, image->file) !=
            FLOPPY_SECTOR_SIZE) {
        floppy_host_error(
            image, true,
            "Floppy read failed at track %u, side %u, sector %u: %s",
            track, side, sector, floppy_system_error());
        return -1;
    }
    image->activity = true;
    return 0;
}

int floppy_image_write_sector(FloppyImage *image, unsigned track,
                              unsigned side, unsigned sector,
                              const u8 data[FLOPPY_SECTOR_SIZE]) {
    FloppyOffset offset;
    size_t written;

    if (!image || !data ||
        image->mode != FLOPPY_IMAGE_READ_WRITE ||
        !sector_offset(image, track, side, sector, &offset))
        return -1;
    clearerr(image->file);
    if (FLOPPY_FSEEK(image->file, offset, SEEK_SET) != 0) {
        floppy_host_error(
            image, true,
            "Floppy seek failed at track %u, side %u, sector %u: %s",
            track, side, sector, floppy_system_error());
        return -1;
    }
    written = fwrite(data, 1, FLOPPY_SECTOR_SIZE, image->file);
    if (written != FLOPPY_SECTOR_SIZE) {
        image->dirty |= written != 0;
        floppy_host_error(
            image, true,
            "Floppy write failed at track %u, side %u, sector %u: %s",
            track, side, sector, floppy_system_error());
        return -1;
    }
    image->dirty = true;
    image->activity = true;
    return 0;
}
