#include "wd2793.h"

#include <string.h>

static FloppyImage *selected_image(Wd2793 *fdc) {
    if (!fdc)
        return NULL;
    if (fdc->selected_drive == 0)
        return &fdc->drive_a;
    if (fdc->selected_drive == 1)
        return &fdc->drive_b;
    return NULL;
}

static const FloppyImage *selected_image_const(const Wd2793 *fdc) {
    if (!fdc)
        return NULL;
    if (fdc->selected_drive == 0)
        return &fdc->drive_a;
    if (fdc->selected_drive == 1)
        return &fdc->drive_b;
    return NULL;
}

static bool drive_ready(const Wd2793 *fdc) {
    return floppy_image_mounted(selected_image_const(fdc));
}

static void end_transfer(Wd2793 *fdc, bool interrupt) {
    fdc->transfer = WD2793_TRANSFER_NONE;
    fdc->transfer_size = 0;
    fdc->transfer_offset = 0;
    fdc->busy = false;
    fdc->drq = false;
    fdc->irq = interrupt;
}

static void command_error(Wd2793 *fdc, u8 status) {
    end_transfer(fdc, true);
    fdc->status_error |= status;
}

static u8 status_register(const Wd2793 *fdc) {
    const FloppyImage *image = selected_image_const(fdc);
    u8 status = fdc ? fdc->status_error : WD2793_STATUS_NOT_READY;

    if (!fdc)
        return status;
    if (!floppy_image_mounted(image))
        status |= WD2793_STATUS_NOT_READY;
    else if (!floppy_image_writable(image))
        status |= WD2793_STATUS_WRITE_PROTECT;
    if (fdc->last_type_one) {
        if (fdc->physical_track == 0)
            status |= WD2793_STATUS_TRACK_ZERO;
    } else if (fdc->drq) {
        status |= WD2793_STATUS_DRQ;
    }
    if (fdc->busy)
        status |= WD2793_STATUS_BUSY;
    return status;
}

static bool load_sector(Wd2793 *fdc) {
    FloppyImage *image = selected_image(fdc);

    if (!drive_ready(fdc)) {
        command_error(fdc, 0);
        return false;
    }
    if (floppy_image_read_sector(
            image, fdc->physical_track, fdc->side_reg & 1,
            fdc->sector, fdc->transfer_data) != 0) {
        command_error(fdc, WD2793_STATUS_RECORD_MISSING);
        return false;
    }
    fdc->transfer = WD2793_TRANSFER_READ;
    fdc->transfer_size = FLOPPY_SECTOR_SIZE;
    fdc->transfer_offset = 0;
    fdc->busy = true;
    fdc->drq = true;
    return true;
}

static bool begin_write_sector(Wd2793 *fdc) {
    FloppyImage *image = selected_image(fdc);

    if (!drive_ready(fdc)) {
        command_error(fdc, 0);
        return false;
    }
    if (!floppy_image_writable(image)) {
        command_error(fdc, 0);
        return false;
    }
    memset(fdc->transfer_data, 0, FLOPPY_SECTOR_SIZE);
    fdc->transfer = WD2793_TRANSFER_WRITE;
    fdc->transfer_size = FLOPPY_SECTOR_SIZE;
    fdc->transfer_offset = 0;
    fdc->busy = true;
    fdc->drq = true;
    return true;
}

static bool continue_multiple(Wd2793 *fdc) {
    FloppyImage *image = selected_image(fdc);

    if (!fdc->multiple || !image)
        return false;
    ++fdc->sector;
    if (fdc->sector > image->sectors_per_track) {
        command_error(fdc, WD2793_STATUS_RECORD_MISSING);
        return true;
    }
    if (fdc->transfer == WD2793_TRANSFER_READ)
        (void)load_sector(fdc);
    else
        (void)begin_write_sector(fdc);
    return true;
}

static void execute_type_one(Wd2793 *fdc, u8 command) {
    unsigned operation = command & 0xf0;
    bool update_track = (command & 0x10) != 0;

    fdc->last_type_one = true;
    switch (operation) {
        case 0x00:
            fdc->physical_track = 0;
            fdc->track = 0;
            fdc->step_direction = -1;
            break;
        case 0x10:
            fdc->step_direction =
                fdc->data >= fdc->physical_track ? 1 : -1;
            fdc->physical_track = fdc->data;
            fdc->track = fdc->data;
            break;
        case 0x20:
        case 0x30:
            if (fdc->step_direction > 0) {
                if (fdc->physical_track < 255)
                    ++fdc->physical_track;
                if (update_track && fdc->track < 255)
                    ++fdc->track;
            } else {
                if (fdc->physical_track)
                    --fdc->physical_track;
                if (update_track && fdc->track)
                    --fdc->track;
            }
            break;
        case 0x40:
        case 0x50:
            fdc->step_direction = 1;
            if (fdc->physical_track < 255)
                ++fdc->physical_track;
            if (update_track && fdc->track < 255)
                ++fdc->track;
            break;
        case 0x60:
        case 0x70:
            fdc->step_direction = -1;
            if (fdc->physical_track)
                --fdc->physical_track;
            if (update_track && fdc->track)
                --fdc->track;
            break;
        default:
            break;
    }
    end_transfer(fdc, true);
}

static void execute_command(Wd2793 *fdc, u8 command) {
    fdc->command = command;
    fdc->irq = false;
    fdc->status_error = 0;
    fdc->multiple = false;
    end_transfer(fdc, false);

    if ((command & 0xf0) == 0xd0) {
        fdc->last_type_one = false;
        fdc->irq = (command & 0x0f) != 0;
        return;
    }
    if ((command & 0x80) == 0) {
        execute_type_one(fdc, command);
        return;
    }

    fdc->last_type_one = false;
    switch (command & 0xe0) {
        case 0x80:
            fdc->multiple = (command & 0x10) != 0;
            (void)load_sector(fdc);
            break;
        case 0xa0:
            fdc->multiple = (command & 0x10) != 0;
            (void)begin_write_sector(fdc);
            break;
        case 0xc0:
            if (!drive_ready(fdc)) {
                command_error(fdc, 0);
                break;
            }
            fdc->transfer_data[0] = fdc->physical_track;
            fdc->transfer_data[1] = fdc->side_reg & 1;
            fdc->transfer_data[2] = fdc->sector ? fdc->sector : 1;
            fdc->transfer_data[3] = 2;
            fdc->transfer_data[4] = 0;
            fdc->transfer_data[5] = 0;
            fdc->transfer = WD2793_TRANSFER_READ;
            fdc->transfer_size = 6;
            fdc->transfer_offset = 0;
            fdc->busy = true;
            fdc->drq = true;
            break;
        case 0xe0:
        default:
            command_error(fdc, WD2793_STATUS_RECORD_MISSING);
            break;
    }
}

static u8 read_data_register(Wd2793 *fdc) {
    u8 value = fdc->data;

    if (fdc->transfer != WD2793_TRANSFER_READ || !fdc->drq)
        return value;
    value = fdc->transfer_data[fdc->transfer_offset++];
    fdc->data = value;
    if (fdc->transfer_offset >= fdc->transfer_size) {
        if (!continue_multiple(fdc))
            end_transfer(fdc, true);
    }
    return value;
}

static void write_data_register(Wd2793 *fdc, u8 value) {
    FloppyImage *image;

    fdc->data = value;
    if (fdc->transfer != WD2793_TRANSFER_WRITE || !fdc->drq)
        return;
    fdc->transfer_data[fdc->transfer_offset++] = value;
    if (fdc->transfer_offset < fdc->transfer_size)
        return;
    image = selected_image(fdc);
    if (!image ||
        floppy_image_write_sector(
            image, fdc->physical_track, fdc->side_reg & 1,
            fdc->sector, fdc->transfer_data) != 0) {
        command_error(
            fdc, floppy_image_writable(image)
                 ? WD2793_STATUS_RECORD_MISSING : 0);
        return;
    }
    if (!continue_multiple(fdc))
        end_transfer(fdc, true);
}

void wd2793_init(Wd2793 *fdc) {
    if (!fdc)
        return;
    memset(fdc, 0, sizeof(*fdc));
    floppy_image_init(&fdc->drive_a);
    floppy_image_init(&fdc->drive_b);
    wd2793_reset(fdc);
}

void wd2793_destroy(Wd2793 *fdc) {
    if (!fdc)
        return;
    floppy_image_destroy(&fdc->drive_a);
    floppy_image_destroy(&fdc->drive_b);
    memset(fdc, 0, sizeof(*fdc));
}

void wd2793_reset(Wd2793 *fdc) {
    if (!fdc)
        return;
    fdc->command = 0;
    fdc->track = 0;
    fdc->sector = 1;
    fdc->data = 0;
    fdc->side_reg = 0;
    fdc->drive_reg = 0;
    fdc->physical_track = 0;
    fdc->step_direction = -1;
    fdc->selected_drive = 0;
    fdc->status_error = 0;
    fdc->last_type_one = true;
    fdc->multiple = false;
    fdc->motor = false;
    end_transfer(fdc, false);
}

bool wd2793_handles_address(u16 address) {
    return (address & 0x3ff8u) == 0x3ff8u;
}

u8 wd2793_read_memory(Wd2793 *fdc, u16 address) {
    u8 reg;

    if (!fdc || !wd2793_handles_address(address))
        return 0xff;
    reg = (u8)(address & 7);
    switch (reg) {
        case 0: {
            u8 status = status_register(fdc);

            fdc->irq = false;
            return status;
        }
        case 1:
            return fdc->track;
        case 2:
            return fdc->sector;
        case 3:
            return read_data_register(fdc);
        case 4:
            return fdc->side_reg;
        case 5: {
            FloppyImage *image = selected_image(fdc);
            u8 result = fdc->drive_reg & (u8)~4u;

            if (!floppy_image_take_disk_changed(image))
                result |= 4;
            return result;
        }
        case 6:
            return 0xff;
        case 7: {
            u8 result = 0xff;

            if (fdc->irq)
                result &= (u8)~0x40u;
            if (fdc->drq)
                result &= (u8)~0x80u;
            return result;
        }
    }
    return 0xff;
}

void wd2793_write_memory(Wd2793 *fdc, u16 address, u8 value) {
    u8 reg;

    if (!fdc || !wd2793_handles_address(address))
        return;
    reg = (u8)(address & 7);
    switch (reg) {
        case 0:
            execute_command(fdc, value);
            break;
        case 1:
            fdc->track = value;
            break;
        case 2:
            fdc->sector = value;
            break;
        case 3:
            write_data_register(fdc, value);
            break;
        case 4:
            fdc->side_reg = value;
            break;
        case 5:
            fdc->drive_reg = value;
            fdc->motor = (value & 0x80) != 0;
            switch (value & 3) {
                case 0:
                case 2:
                    fdc->selected_drive = 0;
                    break;
                case 1:
                    fdc->selected_drive = 1;
                    break;
                default:
                    fdc->selected_drive = -1;
                    break;
            }
            break;
        default:
            break;
    }
}

int wd2793_mount_drive_a(Wd2793 *fdc, const char *path,
                         FloppyImageMode mode) {
    return fdc ? floppy_image_mount(&fdc->drive_a, path, mode) : -1;
}

int wd2793_flush_drive_a(Wd2793 *fdc) {
    return fdc ? floppy_image_flush(&fdc->drive_a) : -1;
}

int wd2793_eject_drive_a(Wd2793 *fdc) {
    if (!fdc)
        return -1;
    if (fdc->selected_drive == 0)
        end_transfer(fdc, false);
    return floppy_image_eject(&fdc->drive_a);
}

bool wd2793_drive_a_mounted(const Wd2793 *fdc) {
    return fdc && floppy_image_mounted(&fdc->drive_a);
}

bool wd2793_drive_a_writable(const Wd2793 *fdc) {
    return fdc && floppy_image_writable(&fdc->drive_a);
}

bool wd2793_drive_a_dirty(const Wd2793 *fdc) {
    return fdc && floppy_image_dirty(&fdc->drive_a);
}

bool wd2793_drive_a_has_error(const Wd2793 *fdc) {
    return fdc && floppy_image_has_error(&fdc->drive_a);
}

const char *wd2793_drive_a_error(const Wd2793 *fdc) {
    return fdc ? floppy_image_error(&fdc->drive_a) : "";
}

bool wd2793_take_drive_a_activity(Wd2793 *fdc) {
    return fdc && floppy_image_take_activity(&fdc->drive_a);
}

int wd2793_mount_drive_b(Wd2793 *fdc, const char *path,
                         FloppyImageMode mode) {
    return fdc ? floppy_image_mount(&fdc->drive_b, path, mode) : -1;
}

int wd2793_flush_drive_b(Wd2793 *fdc) {
    return fdc ? floppy_image_flush(&fdc->drive_b) : -1;
}

int wd2793_eject_drive_b(Wd2793 *fdc) {
    if (!fdc)
        return -1;
    if (fdc->selected_drive == 1)
        end_transfer(fdc, false);
    return floppy_image_eject(&fdc->drive_b);
}

bool wd2793_drive_b_mounted(const Wd2793 *fdc) {
    return fdc && floppy_image_mounted(&fdc->drive_b);
}

bool wd2793_drive_b_writable(const Wd2793 *fdc) {
    return fdc && floppy_image_writable(&fdc->drive_b);
}

bool wd2793_drive_b_dirty(const Wd2793 *fdc) {
    return fdc && floppy_image_dirty(&fdc->drive_b);
}

bool wd2793_drive_b_has_error(const Wd2793 *fdc) {
    return fdc && floppy_image_has_error(&fdc->drive_b);
}

const char *wd2793_drive_b_error(const Wd2793 *fdc) {
    return fdc ? floppy_image_error(&fdc->drive_b) : "";
}

bool wd2793_take_drive_b_activity(Wd2793 *fdc) {
    return fdc && floppy_image_take_activity(&fdc->drive_b);
}
