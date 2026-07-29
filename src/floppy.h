#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "types.h"

#define FLOPPY_SECTOR_SIZE 512u

typedef enum {
    FLOPPY_IMAGE_READ_ONLY = 0,
    FLOPPY_IMAGE_READ_WRITE
} FloppyImageMode;

typedef struct {
    FILE *file;
    FloppyImageMode mode;
    unsigned tracks;
    unsigned sides;
    unsigned sectors_per_track;
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

int floppy_image_read_sector(FloppyImage *image, unsigned track,
                             unsigned side, unsigned sector,
                             u8 data[FLOPPY_SECTOR_SIZE]);
int floppy_image_write_sector(FloppyImage *image, unsigned track,
                              unsigned side, unsigned sector,
                              const u8 data[FLOPPY_SECTOR_SIZE]);
