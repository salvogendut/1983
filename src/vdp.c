#include "vdp.h"

#include <string.h>

/*
 * TMS9918A family palette. Colour zero is transparent on sprites and
 * otherwise resolves to the backdrop colour; the renderer handles that
 * choice before indexing this table.
 */
static const u32 palette[16] = {
    0x000000, 0x000000, 0x3EB849, 0x74D07D,
    0x5955E0, 0x8076F1, 0xB95E51, 0x65DBEF,
    0xDB6559, 0xFF897D, 0xCCC35E, 0xDED087,
    0x3AA241, 0xB766B5, 0xCCCCCC, 0xFFFFFF,
};

static u16 wrap_address(u16 address) {
    return address & (MSX1_VRAM_SIZE - 1);
}

static u8 visible_colour(const MsxVdp *vdp, u8 colour) {
    if ((colour & 0x0f) == 0)
        return vdp->registers[7] & 0x0f;
    return colour & 0x0f;
}

static void put_pixel(MsxVdp *vdp, int x, int y, u8 colour) {
    if ((unsigned)x >= MSX1_VIDEO_W || (unsigned)y >= MSX1_VIDEO_H)
        return;
    vdp->pixels[y * MSX1_VIDEO_W + x] =
        palette[visible_colour(vdp, colour)];
}

void vdp_reset(MsxVdp *vdp) {
    if (!vdp)
        return;
    memset(vdp->vram, 0, sizeof(vdp->vram));
    memset(vdp->registers, 0, sizeof(vdp->registers));
    vdp->status = 0;
    vdp->read_buffer = 0;
    vdp->control_first = 0;
    vdp->address = 0;
    vdp->control_pending = false;
    vdp->irq = false;
    memset(vdp->pixels, 0, sizeof(vdp->pixels));
}

void vdp_init(MsxVdp *vdp) {
    if (!vdp)
        return;
    memset(vdp, 0, sizeof(*vdp));
    vdp_reset(vdp);
}

u8 vdp_read_data(MsxVdp *vdp) {
    u8 value;

    if (!vdp)
        return 0xff;
    value = vdp->read_buffer;
    vdp->read_buffer = vdp->vram[wrap_address(vdp->address)];
    vdp->address = wrap_address(vdp->address + 1);
    vdp->control_pending = false;
    return value;
}

u8 vdp_read_status(MsxVdp *vdp) {
    u8 value;

    if (!vdp)
        return 0xff;
    value = vdp->status;
    vdp->status &= 0x1f;
    vdp->irq = false;
    vdp->control_pending = false;
    return value;
}

void vdp_write_data(MsxVdp *vdp, u8 value) {
    if (!vdp)
        return;
    vdp->vram[wrap_address(vdp->address)] = value;
    vdp->address = wrap_address(vdp->address + 1);
    vdp->read_buffer = value;
    vdp->control_pending = false;
}

void vdp_write_control(MsxVdp *vdp, u8 value) {
    if (!vdp)
        return;
    if (!vdp->control_pending) {
        vdp->control_first = value;
        vdp->control_pending = true;
        return;
    }

    vdp->control_pending = false;
    if (value & 0x80) {
        unsigned reg = value & 0x07;
        vdp->registers[reg] = vdp->control_first;
        if (reg == 1 && !(vdp->registers[1] & 0x20))
            vdp->irq = false;
        return;
    }

    vdp->address = wrap_address(
        ((u16)(value & 0x3f) << 8) | vdp->control_first);
    if (!(value & 0x40)) {
        vdp->read_buffer = vdp->vram[vdp->address];
        vdp->address = wrap_address(vdp->address + 1);
    }
}

void vdp_end_frame(MsxVdp *vdp) {
    if (!vdp)
        return;
    vdp->status |= 0x80;
    if (vdp->registers[1] & 0x20)
        vdp->irq = true;
    vdp_render(vdp);
}

static void render_graphics_1(MsxVdp *vdp) {
    u16 name_base = (u16)(vdp->registers[2] & 0x0f) << 10;
    u16 colour_base = (u16)vdp->registers[3] << 6;
    u16 pattern_base = (u16)(vdp->registers[4] & 0x07) << 11;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int line = y & 7;
        for (int column = 0; column < 32; ++column) {
            u8 name = vdp->vram[wrap_address(
                name_base + row * 32 + column)];
            u8 pattern = vdp->vram[wrap_address(
                pattern_base + name * 8 + line)];
            u8 colours = vdp->vram[wrap_address(
                colour_base + (name >> 3))];
            for (int bit = 0; bit < 8; ++bit) {
                u8 colour = pattern & (0x80 >> bit)
                          ? colours >> 4 : colours & 0x0f;
                put_pixel(vdp, column * 8 + bit, y, colour);
            }
        }
    }
}

static void render_graphics_2(MsxVdp *vdp) {
    u16 name_base = (u16)(vdp->registers[2] & 0x0f) << 10;
    u16 pattern_base = (u16)(vdp->registers[4] & 0x04) << 11;
    u16 colour_base = (u16)(vdp->registers[3] & 0x80) << 6;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int line = y & 7;
        int third = (y >> 6) * 0x800;
        for (int column = 0; column < 32; ++column) {
            u8 name = vdp->vram[wrap_address(
                name_base + row * 32 + column)];
            u16 offset = (u16)(third + name * 8 + line);
            u8 pattern = vdp->vram[wrap_address(pattern_base + offset)];
            u8 colours = vdp->vram[wrap_address(colour_base + offset)];
            for (int bit = 0; bit < 8; ++bit) {
                u8 colour = pattern & (0x80 >> bit)
                          ? colours >> 4 : colours & 0x0f;
                put_pixel(vdp, column * 8 + bit, y, colour);
            }
        }
    }
}

static void render_text(MsxVdp *vdp) {
    u16 name_base = (u16)(vdp->registers[2] & 0x0f) << 10;
    u16 pattern_base = (u16)(vdp->registers[4] & 0x07) << 11;
    u8 foreground = vdp->registers[7] >> 4;
    u8 background = vdp->registers[7] & 0x0f;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int line = y & 7;
        for (int column = 0; column < 40; ++column) {
            u8 name = vdp->vram[wrap_address(
                name_base + row * 40 + column)];
            u8 pattern = vdp->vram[wrap_address(
                pattern_base + name * 8 + line)];
            for (int bit = 0; bit < 6; ++bit)
                put_pixel(vdp, 8 + column * 6 + bit, y,
                          pattern & (0x80 >> bit)
                          ? foreground : background);
        }
    }
}

static void render_multicolour(MsxVdp *vdp) {
    u16 name_base = (u16)(vdp->registers[2] & 0x0f) << 10;
    u16 pattern_base = (u16)(vdp->registers[4] & 0x07) << 11;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int pattern_line = ((y & 0x1c) >> 2);
        for (int column = 0; column < 32; ++column) {
            u8 name = vdp->vram[wrap_address(
                name_base + row * 32 + column)];
            u8 colours = vdp->vram[wrap_address(
                pattern_base + name * 8 + pattern_line)];
            for (int x = 0; x < 8; ++x)
                put_pixel(vdp, column * 8 + x, y,
                          x < 4 ? colours >> 4 : colours & 0x0f);
        }
    }
}

void vdp_render(MsxVdp *vdp) {
    u8 backdrop;

    if (!vdp)
        return;
    backdrop = vdp->registers[7] & 0x0f;
    for (size_t i = 0;
         i < sizeof(vdp->pixels) / sizeof(vdp->pixels[0]); ++i)
        vdp->pixels[i] = palette[backdrop];

    if (!(vdp->registers[1] & 0x40))
        return;
    if (vdp->registers[1] & 0x10)
        render_text(vdp);
    else if (vdp->registers[1] & 0x08)
        render_graphics_2(vdp);
    else if (vdp->registers[0] & 0x02)
        render_multicolour(vdp);
    else
        render_graphics_1(vdp);
}
