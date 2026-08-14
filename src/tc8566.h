#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "floppy.h"
#include "types.h"

#define TC8566_MAIN_DRIVE_0 0x01u
#define TC8566_MAIN_DRIVE_1 0x02u
#define TC8566_MAIN_BUSY    0x10u
#define TC8566_MAIN_NONDMA  0x20u
#define TC8566_MAIN_DIO     0x40u
#define TC8566_MAIN_RQM     0x80u

typedef enum {
    TC8566_PHASE_IDLE = 0,
    TC8566_PHASE_COMMAND,
    TC8566_PHASE_READ,
    TC8566_PHASE_WRITE,
    TC8566_PHASE_FORMAT,
    TC8566_PHASE_RESULT
} Tc8566Phase;

typedef enum {
    TC8566_COMMAND_NONE = 0,
    TC8566_COMMAND_READ_DATA,
    TC8566_COMMAND_WRITE_DATA,
    TC8566_COMMAND_READ_ID,
    TC8566_COMMAND_FORMAT,
    TC8566_COMMAND_RECALIBRATE,
    TC8566_COMMAND_SENSE_INTERRUPT,
    TC8566_COMMAND_SPECIFY,
    TC8566_COMMAND_SENSE_DRIVE,
    TC8566_COMMAND_SEEK
} Tc8566Command;

typedef struct {
    FloppyImage *drives[2];
    Tc8566Phase phase;
    Tc8566Command command;
    u8 main_status;
    u8 control;
    u8 selected_drive;
    bool motor[2];

    u8 command_code;
    u8 parameters[8];
    size_t parameter_count;
    size_t parameter_needed;

    u8 result[7];
    size_t result_count;
    size_t result_position;

    u8 status0;
    u8 status1;
    u8 status2;
    u8 status3;
    u8 cylinder;
    u8 head;
    u8 sector;
    u8 size_code;
    u8 end_of_track;
    u8 current_track[2];

    u8 transfer[FLOPPY_SECTOR_SIZE];
    size_t transfer_position;
    size_t transfer_size;

    u8 format_tuple[4];
    size_t format_tuple_position;
    unsigned format_remaining;
    u8 format_filler;

    bool interrupt_pending;
    u8 interrupt_status;
    u8 interrupt_track;
} Tc8566;

void tc8566_init(Tc8566 *fdc, FloppyImage *drive_a,
                 FloppyImage *drive_b);
void tc8566_reset(Tc8566 *fdc);

u8 tc8566_read_status(const Tc8566 *fdc);
u8 tc8566_read_data(Tc8566 *fdc);
void tc8566_write_control(Tc8566 *fdc, u8 value);
void tc8566_write_data(Tc8566 *fdc, u8 value);

u8 tc8566_read_memory(Tc8566 *fdc, u16 address);
void tc8566_write_memory(Tc8566 *fdc, u16 address, u8 value);
