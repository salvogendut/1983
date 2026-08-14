#include "tc8566.h"

#include <string.h>

#define TC8566_ST0_NR  0x08u
#define TC8566_ST0_SE  0x20u
#define TC8566_ST0_IC0 0x40u
#define TC8566_ST0_IC1 0x80u

#define TC8566_ST1_MA 0x01u
#define TC8566_ST1_NW 0x02u
#define TC8566_ST1_ND 0x04u

#define TC8566_ST3_HD  0x04u
#define TC8566_ST3_2S  0x08u
#define TC8566_ST3_TK0 0x10u
#define TC8566_ST3_RDY 0x20u
#define TC8566_ST3_WP  0x40u

static FloppyImage *selected_image(Tc8566 *fdc) {
    if (!fdc || fdc->selected_drive >= 2)
        return NULL;
    return fdc->drives[fdc->selected_drive];
}

static const FloppyImage *selected_image_const(const Tc8566 *fdc) {
    if (!fdc || fdc->selected_drive >= 2)
        return NULL;
    return fdc->drives[fdc->selected_drive];
}

static void enter_idle(Tc8566 *fdc) {
    fdc->phase = TC8566_PHASE_IDLE;
    fdc->command = TC8566_COMMAND_NONE;
    fdc->main_status = TC8566_MAIN_RQM;
    fdc->parameter_count = 0;
    fdc->parameter_needed = 0;
    fdc->transfer_position = 0;
    fdc->transfer_size = 0;
}

static void enter_result(Tc8566 *fdc, const u8 *result, size_t count) {
    if (count > sizeof(fdc->result))
        count = sizeof(fdc->result);
    if (count)
        memcpy(fdc->result, result, count);
    fdc->result_count = count;
    fdc->result_position = 0;
    fdc->phase = TC8566_PHASE_RESULT;
    fdc->main_status = TC8566_MAIN_RQM |
                       TC8566_MAIN_BUSY |
                       TC8566_MAIN_DIO;
}

static u8 drive_head(const Tc8566 *fdc) {
    return (u8)((fdc->parameters[0] & 3u) |
                (fdc->head ? 4u : 0u));
}

static void enter_rw_result(Tc8566 *fdc) {
    u8 result[7];

    result[0] = fdc->status0;
    result[1] = fdc->status1;
    result[2] = fdc->status2;
    result[3] = fdc->cylinder;
    result[4] = fdc->head;
    result[5] = fdc->sector;
    result[6] = fdc->size_code;
    enter_result(fdc, result, sizeof(result));
}

static void set_command_error(Tc8566 *fdc, u8 status0, u8 status1) {
    fdc->status0 = (u8)(drive_head(fdc) | TC8566_ST0_IC0 | status0);
    fdc->status1 = status1;
    fdc->status2 = 0;
    enter_rw_result(fdc);
}

static bool rw_request_valid(Tc8566 *fdc, bool write) {
    FloppyImage *image = selected_image(fdc);

    if (!floppy_image_mounted(image)) {
        set_command_error(fdc, TC8566_ST0_NR, TC8566_ST1_MA);
        return false;
    }
    if (write && !floppy_image_writable(image)) {
        set_command_error(fdc, 0, TC8566_ST1_NW);
        return false;
    }
    if (fdc->size_code != 2 ||
        fdc->selected_drive >= 2 ||
        fdc->cylinder != fdc->current_track[fdc->selected_drive] ||
        fdc->cylinder >= image->tracks ||
        fdc->head >= image->sides ||
        fdc->sector < 1 ||
        fdc->sector > image->sectors_per_track) {
        set_command_error(fdc, 0, TC8566_ST1_ND);
        return false;
    }
    return true;
}

static bool load_read_sector(Tc8566 *fdc) {
    FloppyImage *image = selected_image(fdc);

    if (!rw_request_valid(fdc, false))
        return false;
    if (floppy_image_read_sector(
            image, fdc->cylinder, fdc->head,
            fdc->sector, fdc->transfer) != 0) {
        set_command_error(fdc, 0, TC8566_ST1_MA);
        return false;
    }
    fdc->transfer_position = 0;
    fdc->transfer_size = FLOPPY_SECTOR_SIZE;
    fdc->phase = TC8566_PHASE_READ;
    fdc->main_status = TC8566_MAIN_RQM |
                       TC8566_MAIN_BUSY |
                       TC8566_MAIN_NONDMA |
                       TC8566_MAIN_DIO;
    return true;
}

static bool begin_write_sector(Tc8566 *fdc) {
    if (!rw_request_valid(fdc, true))
        return false;
    memset(fdc->transfer, 0, sizeof(fdc->transfer));
    fdc->transfer_position = 0;
    fdc->transfer_size = FLOPPY_SECTOR_SIZE;
    fdc->phase = TC8566_PHASE_WRITE;
    fdc->main_status = TC8566_MAIN_RQM |
                       TC8566_MAIN_BUSY |
                       TC8566_MAIN_NONDMA;
    return true;
}

static bool next_sector(Tc8566 *fdc, bool write) {
    const FloppyImage *image = selected_image_const(fdc);

    if (!image || fdc->sector >= fdc->end_of_track ||
        fdc->sector >= image->sectors_per_track) {
        ++fdc->sector;
        enter_rw_result(fdc);
        return false;
    }
    ++fdc->sector;
    return write ? begin_write_sector(fdc) : load_read_sector(fdc);
}

static void prepare_read_write(Tc8566 *fdc, bool write) {
    fdc->selected_drive = fdc->control & 3u;
    fdc->cylinder = fdc->parameters[1];
    fdc->head = fdc->parameters[2];
    fdc->sector = fdc->parameters[3];
    fdc->size_code = fdc->parameters[4];
    fdc->end_of_track = fdc->parameters[5];
    fdc->status0 = drive_head(fdc);
    fdc->status1 = 0;
    fdc->status2 = 0;
    if (write)
        (void)begin_write_sector(fdc);
    else
        (void)load_read_sector(fdc);
}

static void prepare_read_id(Tc8566 *fdc) {
    const FloppyImage *image;

    fdc->selected_drive = fdc->control & 3u;
    fdc->head = (fdc->parameters[0] >> 2) & 1u;
    fdc->sector = 1;
    fdc->size_code = 2;
    fdc->cylinder = fdc->selected_drive < 2
                  ? fdc->current_track[fdc->selected_drive] : 0;
    fdc->status0 = drive_head(fdc);
    fdc->status1 = 0;
    fdc->status2 = 0;
    image = selected_image_const(fdc);
    if (!floppy_image_mounted(image)) {
        set_command_error(fdc, TC8566_ST0_NR, TC8566_ST1_MA);
        return;
    }
    if (fdc->head >= image->sides || fdc->cylinder >= image->tracks) {
        set_command_error(fdc, 0, TC8566_ST1_ND);
        return;
    }
    enter_rw_result(fdc);
}

static void prepare_format(Tc8566 *fdc) {
    FloppyImage *image;

    fdc->selected_drive = fdc->control & 3u;
    fdc->head = (fdc->parameters[0] >> 2) & 1u;
    fdc->cylinder = fdc->selected_drive < 2
                  ? fdc->current_track[fdc->selected_drive] : 0;
    fdc->size_code = fdc->parameters[1];
    fdc->format_remaining = fdc->parameters[2];
    fdc->format_filler = fdc->parameters[4];
    fdc->format_tuple_position = 0;
    fdc->sector = 1;
    fdc->status0 = drive_head(fdc);
    fdc->status1 = 0;
    fdc->status2 = 0;
    image = selected_image(fdc);
    if (!floppy_image_mounted(image)) {
        set_command_error(fdc, TC8566_ST0_NR, TC8566_ST1_MA);
        return;
    }
    if (!floppy_image_writable(image)) {
        set_command_error(fdc, 0, TC8566_ST1_NW);
        return;
    }
    if (fdc->size_code != 2 || !fdc->format_remaining ||
        fdc->head >= image->sides || fdc->cylinder >= image->tracks) {
        set_command_error(fdc, 0, TC8566_ST1_ND);
        return;
    }
    fdc->phase = TC8566_PHASE_FORMAT;
    fdc->main_status = TC8566_MAIN_RQM |
                       TC8566_MAIN_BUSY |
                       TC8566_MAIN_NONDMA;
}

static void complete_command_parameters(Tc8566 *fdc) {
    u8 result[2];
    FloppyImage *image;
    unsigned drive;

    switch (fdc->command) {
        case TC8566_COMMAND_READ_DATA:
            prepare_read_write(fdc, false);
            break;
        case TC8566_COMMAND_WRITE_DATA:
            prepare_read_write(fdc, true);
            break;
        case TC8566_COMMAND_READ_ID:
            prepare_read_id(fdc);
            break;
        case TC8566_COMMAND_FORMAT:
            prepare_format(fdc);
            break;
        case TC8566_COMMAND_RECALIBRATE:
            drive = fdc->parameters[0] & 3u;
            fdc->selected_drive = (u8)drive;
            if (drive < 2)
                fdc->current_track[drive] = 0;
            fdc->interrupt_status = (u8)(drive | TC8566_ST0_SE |
                (drive < 2 ? 0 : TC8566_ST0_IC1 | TC8566_ST0_NR));
            fdc->interrupt_track = 0;
            fdc->interrupt_pending = true;
            enter_idle(fdc);
            break;
        case TC8566_COMMAND_SEEK:
            drive = fdc->parameters[0] & 3u;
            fdc->selected_drive = (u8)drive;
            if (drive < 2)
                fdc->current_track[drive] = fdc->parameters[1];
            fdc->interrupt_status = (u8)(drive | TC8566_ST0_SE |
                (drive < 2 ? 0 : TC8566_ST0_IC1 | TC8566_ST0_NR));
            fdc->interrupt_track = drive < 2
                                 ? fdc->current_track[drive] : 0;
            fdc->interrupt_pending = true;
            enter_idle(fdc);
            break;
        case TC8566_COMMAND_SPECIFY:
            enter_idle(fdc);
            break;
        case TC8566_COMMAND_SENSE_DRIVE:
            drive = fdc->parameters[0] & 3u;
            fdc->selected_drive = (u8)drive;
            image = selected_image(fdc);
            fdc->status3 = (u8)(fdc->parameters[0] & 7u);
            if (drive < 2 && fdc->current_track[drive] == 0)
                fdc->status3 |= TC8566_ST3_TK0;
            if (floppy_image_mounted(image)) {
                fdc->status3 |= TC8566_ST3_RDY;
                if (image->sides > 1)
                    fdc->status3 |= TC8566_ST3_HD | TC8566_ST3_2S;
                if (!floppy_image_writable(image))
                    fdc->status3 |= TC8566_ST3_WP;
            }
            result[0] = fdc->status3;
            enter_result(fdc, result, 1);
            break;
        case TC8566_COMMAND_SENSE_INTERRUPT:
        case TC8566_COMMAND_NONE:
            break;
    }
}

static void begin_command(Tc8566 *fdc, u8 value) {
    u8 result[2];

    fdc->command_code = value;
    fdc->parameter_count = 0;
    fdc->main_status = TC8566_MAIN_RQM | TC8566_MAIN_BUSY;
    if ((value & 0x1fu) == 0x06u) {
        fdc->command = TC8566_COMMAND_READ_DATA;
        fdc->parameter_needed = 8;
    } else if ((value & 0x3fu) == 0x05u) {
        fdc->command = TC8566_COMMAND_WRITE_DATA;
        fdc->parameter_needed = 8;
    } else if ((value & 0xbfu) == 0x0au) {
        fdc->command = TC8566_COMMAND_READ_ID;
        fdc->parameter_needed = 1;
    } else if ((value & 0xbfu) == 0x0du) {
        fdc->command = TC8566_COMMAND_FORMAT;
        fdc->parameter_needed = 5;
    } else if (value == 0x07u) {
        fdc->command = TC8566_COMMAND_RECALIBRATE;
        fdc->parameter_needed = 1;
    } else if (value == 0x08u) {
        fdc->command = TC8566_COMMAND_SENSE_INTERRUPT;
        result[0] = fdc->interrupt_pending
                  ? fdc->interrupt_status : TC8566_ST0_IC1;
        result[1] = fdc->interrupt_pending
                  ? fdc->interrupt_track : 0;
        fdc->interrupt_pending = false;
        enter_result(fdc, result, 2);
        return;
    } else if (value == 0x03u) {
        fdc->command = TC8566_COMMAND_SPECIFY;
        fdc->parameter_needed = 2;
    } else if (value == 0x04u) {
        fdc->command = TC8566_COMMAND_SENSE_DRIVE;
        fdc->parameter_needed = 1;
    } else if (value == 0x0fu) {
        fdc->command = TC8566_COMMAND_SEEK;
        fdc->parameter_needed = 2;
    } else {
        result[0] = TC8566_ST0_IC1;
        fdc->command = TC8566_COMMAND_NONE;
        enter_result(fdc, result, 1);
        return;
    }
    fdc->phase = TC8566_PHASE_COMMAND;
}

void tc8566_init(Tc8566 *fdc, FloppyImage *drive_a,
                 FloppyImage *drive_b) {
    if (!fdc)
        return;
    memset(fdc, 0, sizeof(*fdc));
    fdc->drives[0] = drive_a;
    fdc->drives[1] = drive_b;
    tc8566_reset(fdc);
}

void tc8566_reset(Tc8566 *fdc) {
    FloppyImage *drive_a;
    FloppyImage *drive_b;

    if (!fdc)
        return;
    drive_a = fdc->drives[0];
    drive_b = fdc->drives[1];
    memset(fdc, 0, sizeof(*fdc));
    fdc->drives[0] = drive_a;
    fdc->drives[1] = drive_b;
    fdc->main_status = TC8566_MAIN_RQM;
}

u8 tc8566_read_status(const Tc8566 *fdc) {
    return fdc ? fdc->main_status : 0xff;
}

u8 tc8566_read_data(Tc8566 *fdc) {
    u8 value;

    if (!fdc)
        return 0xff;
    if (fdc->phase == TC8566_PHASE_RESULT) {
        if (fdc->result_position >= fdc->result_count)
            return 0xff;
        value = fdc->result[fdc->result_position++];
        if (fdc->result_position == fdc->result_count)
            enter_idle(fdc);
        return value;
    }
    if (fdc->phase != TC8566_PHASE_READ ||
        fdc->transfer_position >= fdc->transfer_size)
        return 0xff;
    value = fdc->transfer[fdc->transfer_position++];
    if (fdc->transfer_position == fdc->transfer_size)
        (void)next_sector(fdc, false);
    return value;
}

void tc8566_write_control(Tc8566 *fdc, u8 value) {
    if (!fdc)
        return;
    fdc->control = value;
    fdc->selected_drive = value & 3u;
    fdc->motor[0] = (value & 0x10u) != 0;
    fdc->motor[1] = (value & 0x20u) != 0;
}

static void format_data_write(Tc8566 *fdc, u8 value) {
    FloppyImage *image = selected_image(fdc);

    fdc->format_tuple[fdc->format_tuple_position++] = value;
    if (fdc->format_tuple_position < sizeof(fdc->format_tuple))
        return;
    fdc->format_tuple_position = 0;
    fdc->cylinder = fdc->format_tuple[0];
    fdc->head = fdc->format_tuple[1];
    fdc->sector = fdc->format_tuple[2];
    fdc->size_code = fdc->format_tuple[3];
    if (fdc->size_code != 2 || !image) {
        set_command_error(fdc, 0, TC8566_ST1_NW);
        return;
    }
    memset(fdc->transfer, fdc->format_filler, sizeof(fdc->transfer));
    if (floppy_image_write_sector(
            image, fdc->cylinder, fdc->head, fdc->sector,
            fdc->transfer) != 0) {
        set_command_error(fdc, 0, TC8566_ST1_NW);
        return;
    }
    if (--fdc->format_remaining == 0)
        enter_rw_result(fdc);
}

void tc8566_write_data(Tc8566 *fdc, u8 value) {
    FloppyImage *image;

    if (!fdc)
        return;
    if (fdc->phase == TC8566_PHASE_IDLE) {
        begin_command(fdc, value);
        return;
    }
    if (fdc->phase == TC8566_PHASE_COMMAND) {
        if (fdc->parameter_count < sizeof(fdc->parameters))
            fdc->parameters[fdc->parameter_count++] = value;
        if (fdc->parameter_count == fdc->parameter_needed)
            complete_command_parameters(fdc);
        return;
    }
    if (fdc->phase == TC8566_PHASE_FORMAT) {
        format_data_write(fdc, value);
        return;
    }
    if (fdc->phase != TC8566_PHASE_WRITE ||
        fdc->transfer_position >= fdc->transfer_size)
        return;
    fdc->transfer[fdc->transfer_position++] = value;
    if (fdc->transfer_position < fdc->transfer_size)
        return;
    image = selected_image(fdc);
    if (!image || floppy_image_write_sector(
            image, fdc->cylinder, fdc->head,
            fdc->sector, fdc->transfer) != 0) {
        set_command_error(fdc, 0, TC8566_ST1_NW);
        return;
    }
    (void)next_sector(fdc, true);
}

u8 tc8566_read_memory(Tc8566 *fdc, u16 address) {
    address &= 0x7fffu;
    if (address >= 0x1000u)
        return 0xff;
    return address & 1u
         ? tc8566_read_data(fdc) : tc8566_read_status(fdc);
}

void tc8566_write_memory(Tc8566 *fdc, u16 address, u8 value) {
    address &= 0x7fffu;
    if (address < 0x1000u) {
        if (address & 1u)
            tc8566_write_data(fdc, value);
    } else if (address < 0x2000u) {
        tc8566_write_control(fdc, value);
    }
}
