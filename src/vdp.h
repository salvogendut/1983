#pragma once

#include <stdbool.h>

#include "types.h"

#define MSX1_VRAM_SIZE 0x4000
#define MSX1_VIDEO_W 256
#define MSX1_VIDEO_H 192

typedef struct {
    u8 vram[MSX1_VRAM_SIZE];
    u8 registers[8];
    u8 status;
    u8 read_buffer;
    u8 control_first;
    u16 address;
    bool control_pending;
    bool irq;
    u32 pixels[MSX1_VIDEO_W * MSX1_VIDEO_H];
} MsxVdp;

void vdp_init(MsxVdp *vdp);
void vdp_reset(MsxVdp *vdp);

u8   vdp_read_data(MsxVdp *vdp);
u8   vdp_read_status(MsxVdp *vdp);
void vdp_write_data(MsxVdp *vdp, u8 value);
void vdp_write_control(MsxVdp *vdp, u8 value);

void vdp_end_frame(MsxVdp *vdp);
void vdp_render(MsxVdp *vdp);

