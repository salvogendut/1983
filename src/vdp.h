#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "types.h"

#define MSX1_VRAM_SIZE 0x4000
#define MSX2_VRAM_SIZE 0x20000
#define MSX_VDP_REGISTER_COUNT 64
#define MSX_VDP_PALETTE_SIZE 16
#define MSX1_VIDEO_W 256
#define MSX1_VIDEO_H 192
#define MSX2_VIDEO_W 512
#define MSX2_VIDEO_H 212

typedef enum {
    MSX_VDP_TMS9918 = 0,
    MSX_VDP_V9938,
    MSX_VDP_V9958
} MsxVdpType;

static inline bool vdp_type_is_v99x8(MsxVdpType type) {
    return type == MSX_VDP_V9938 || type == MSX_VDP_V9958;
}

typedef struct {
    u8 vram[MSX2_VRAM_SIZE];
    u8 registers[MSX_VDP_REGISTER_COUNT];
    u8 status;
    u8 status1;
    u8 status2;
    u8 status7;
    u8 read_buffer;
    u8 control_first;
    u16 address;
    u16 palette_grb[MSX_VDP_PALETTE_SIZE];
    MsxVdpType type;
    bool control_pending;
    bool palette_pending;
    bool irq;
    u16 command_x;
    u16 command_y;
    u16 command_origin_x;
    u16 command_source_x;
    u16 command_source_y;
    u16 command_source_origin_x;
    u16 command_row_length;
    u16 command_remaining_x;
    u16 command_remaining_y;
    u16 command_border_x;
    u16 command_line_major;
    u16 command_line_minor;
    u16 command_line_error;
    u16 sprite_collision_x;
    u16 sprite_collision_y;
    u8 command_code;
    u8 command_mode;
    u8 command_argument;
    u8 command_colour;
    u8 command_operation;
    u8 command_event;
    bool command_transfer_pending;
    bool cpu_vram_pending;
    bool cpu_vram_request_read;
    u64 command_ticks_remaining;
    u64 cpu_vram_ticks_remaining;
    u64 vram_tick_fraction;
    u32 vram_frame_tick;
    unsigned timing_cycle;
    unsigned timing_frame_cycles;
    unsigned timing_scanlines;
    unsigned render_width;
    unsigned render_height;
    unsigned render_line_zero;
    unsigned rendered_lines;
    u32 pixels[MSX2_VIDEO_W * MSX2_VIDEO_H];
} MsxVdp;

void vdp_init(MsxVdp *vdp);
void vdp_reset(MsxVdp *vdp);
void vdp_set_type(MsxVdp *vdp, MsxVdpType type);

u8   vdp_read_data(MsxVdp *vdp);
u8   vdp_read_status(MsxVdp *vdp);
void vdp_write_data(MsxVdp *vdp, u8 value);
void vdp_write_control(MsxVdp *vdp, u8 value);
void vdp_write_palette(MsxVdp *vdp, u8 value);
void vdp_write_indirect(MsxVdp *vdp, u8 value);

void vdp_begin_frame(MsxVdp *vdp, unsigned frame_cycles,
                     unsigned scanlines);
void vdp_advance(MsxVdp *vdp, unsigned cycles);
void vdp_end_frame(MsxVdp *vdp);
void vdp_render(MsxVdp *vdp);
/* SCREEN 6 alternates its two border colours on high-resolution dots. */
u32  vdp_border_colour(const MsxVdp *vdp, unsigned high_res_phase);

/* Decode a SCREEN 0 text-mode name table into ASCII (40x24) on `out`.
 * Printable character codes 0x20-0x7e are emitted directly; anything else
 * becomes '.'. Prints a note and returns when the VDP is not in text mode. */
void vdp_dump_screen_text(const MsxVdp *vdp, FILE *out);
