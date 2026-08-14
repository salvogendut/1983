#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "floppy.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
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

static bool inspect_image(FILE *file, FloppyOffset size,
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
    memset(image, 0, sizeof(*image));
}

int floppy_image_mount(FloppyImage *image, const char *path,
                       FloppyImageMode mode) {
    FILE *file;
    FloppyOffset size;
    unsigned tracks;
    unsigned sides;
    unsigned sectors_per_track;

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
        (size = FLOPPY_FTELL(file)) < 0 ||
        !inspect_image(file, size, &tracks, &sides,
                       &sectors_per_track)) {
        int saved_errno = errno;

        fclose(file);
        floppy_host_error(
            image, false,
            "Unsupported raw DSK geometry%s%s",
            saved_errno ? ": " : "",
            saved_errno ? strerror(saved_errno) : "");
        return -1;
    }
    if (image->file && floppy_image_eject(image) != 0) {
        fclose(file);
        return -1;
    }
    image->file = file;
    image->mode = mode;
    image->tracks = tracks;
    image->sides = sides;
    image->sectors_per_track = sectors_per_track;
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
    image->tracks = 0;
    image->sides = 0;
    image->sectors_per_track = 0;
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

static bool sector_offset(const FloppyImage *image, unsigned track,
                          unsigned side, unsigned sector,
                          FloppyOffset *offset) {
    u64 logical_sector;

    if (!image || !image->file ||
        track >= image->tracks || side >= image->sides ||
        sector < 1 || sector > image->sectors_per_track)
        return false;
    logical_sector =
        ((u64)track * image->sides + side) *
        image->sectors_per_track + (sector - 1);
    *offset = (FloppyOffset)(logical_sector * FLOPPY_SECTOR_SIZE);
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
