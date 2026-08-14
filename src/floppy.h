#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "types.h"

#define FLOPPY_SECTOR_SIZE 512u
#define FLOPPY_MAX_SECTORS_PER_TRACK 29u

typedef enum {
    FLOPPY_IMAGE_READ_ONLY = 0,
    FLOPPY_IMAGE_READ_WRITE
} FloppyImageMode;

typedef enum {
    FLOPPY_FORMAT_RAW = 0,
    FLOPPY_FORMAT_CPC_DSK
} FloppyImageFormat;

typedef struct {
    u8 id;
    u8 size_code;
    u8 st1;
    u8 st2;
    unsigned size;
    u64 offset;
} FloppySectorInfo;

typedef struct {
    unsigned sector_count;
    FloppySectorInfo sectors[FLOPPY_MAX_SECTORS_PER_TRACK];
} FloppyTrackInfo;

typedef struct {
    FILE *file;
    FloppyImageMode mode;
    FloppyImageFormat format;
    unsigned tracks;
    unsigned sides;
    unsigned sectors_per_track;
    FloppyTrackInfo *track_info;
    bool dirty;
    bool activity;
    bool disk_changed;
    bool io_error;
    char host_error[192];
} FloppyImage;

void floppy_image_init(FloppyImage *image);
void floppy_image_destroy(FloppyImage *image);

int floppy_image_mount(FloppyImage *image, const char *path,
                       FloppyImageMode mode);
int floppy_image_flush(FloppyImage *image);
int floppy_image_eject(FloppyImage *image);

bool floppy_image_mounted(const FloppyImage *image);
bool floppy_image_writable(const FloppyImage *image);
bool floppy_image_dirty(const FloppyImage *image);
bool floppy_image_has_error(const FloppyImage *image);
const char *floppy_image_error(const FloppyImage *image);
bool floppy_image_take_activity(FloppyImage *image);
bool floppy_image_take_disk_changed(FloppyImage *image);
bool floppy_image_first_sector(const FloppyImage *image,
                               unsigned track, unsigned side,
                               unsigned *sector);
bool floppy_image_next_sector(const FloppyImage *image,
                              unsigned track, unsigned side,
                              unsigned sector, unsigned *next_sector);
bool floppy_image_sector_info(const FloppyImage *image,
                              unsigned track, unsigned side,
                              unsigned sector, u8 *size_code,
                              u8 *st1, u8 *st2);

int floppy_image_read_sector(FloppyImage *image, unsigned track,
                             unsigned side, unsigned sector,
                             u8 data[FLOPPY_SECTOR_SIZE]);
int floppy_image_write_sector(FloppyImage *image, unsigned track,
                              unsigned side, unsigned sector,
                              const u8 data[FLOPPY_SECTOR_SIZE]);
