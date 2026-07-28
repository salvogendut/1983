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

typedef struct {
    int x;
    u16 pattern;
    u8 colour;
} SpriteLine;

static u16 sprite_pattern_line(const MsxVdp *vdp, u8 pattern_number,
                               int size, unsigned line) {
    u16 pattern_base = (u16)(vdp->registers[6] & 0x07) << 11;
    unsigned left_pattern;
    unsigned row = line & 7;
    u16 bits;

    if (size == 16)
        pattern_number &= 0xfc;
    left_pattern = pattern_number + (line >= 8 ? 1u : 0u);
    bits = (u16)vdp->vram[wrap_address(
        pattern_base + left_pattern * 8 + row)] << 8;
    if (size == 16) {
        bits |= vdp->vram[wrap_address(
            pattern_base + (left_pattern + 2) * 8 + row)];
    }
    return bits;
}

static void update_sprite_status(MsxVdp *vdp, int fifth_sprite,
                                 unsigned last_sprite, bool collision) {
    u8 status = vdp->status;

    if (fifth_sprite >= 0) {
        /*
         * On TMS9918-family VDPs, fifth-sprite detection only latches while
         * both the vertical-blank and fifth-sprite flags are clear.
         */
        if (!(status & 0xc0))
            status = (status & 0xa0) | 0x40 | (u8)fifth_sprite;
    } else if (!(status & 0x40)) {
        status = (status & 0xa0) | (u8)(last_sprite & 0x1f);
    }
    if (collision)
        status |= 0x20;
    vdp->status = status;
}

static void render_sprites(MsxVdp *vdp) {
    u16 attribute_base = (u16)(vdp->registers[5] & 0x7f) << 7;
    int size = vdp->registers[1] & 0x02 ? 16 : 8;
    int scale = vdp->registers[1] & 0x01 ? 2 : 1;
    int effective_size = size * scale;
    unsigned sprite_end = 0;
    unsigned last_sprite;
    int fifth_sprite = -1;
    bool collision = false;

    while (sprite_end < 32 &&
           vdp->vram[wrap_address(attribute_base + sprite_end * 4)] != 0xd0)
        ++sprite_end;
    last_sprite = sprite_end < 32 ? sprite_end : 31;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        SpriteLine visible[4];
        unsigned visible_count = 0;
        bool occupied[MSX1_VIDEO_W] = { false };
        bool coloured[MSX1_VIDEO_W] = { false };

        for (unsigned sprite = 0; sprite < sprite_end; ++sprite) {
            u16 offset = wrap_address(attribute_base + sprite * 4);
            u8 raw_y = vdp->vram[offset];
            unsigned top = ((unsigned)raw_y + 1) & 0xff;
            unsigned sprite_line = ((unsigned)y + 256 - top) & 0xff;
            u8 colour_attribute;

            if (sprite_line >= (unsigned)effective_size)
                continue;
            if (visible_count == 4) {
                if (fifth_sprite < 0)
                    fifth_sprite = (int)sprite;
                continue;
            }

            colour_attribute = vdp->vram[wrap_address(offset + 3)];
            visible[visible_count].x =
                vdp->vram[wrap_address(offset + 1)]
                - (colour_attribute & 0x80 ? 32 : 0);
            visible[visible_count].pattern = sprite_pattern_line(
                vdp, vdp->vram[wrap_address(offset + 2)], size,
                sprite_line / (unsigned)scale);
            visible[visible_count].colour = colour_attribute & 0x0f;
            ++visible_count;
        }

        /*
         * Attribute order is sprite priority order. Drawing from low to high
         * and remembering coloured pixels prevents a lower-priority sprite
         * from overwriting a higher-priority one. Transparent sprite pixels
         * do not hide later sprites, but their pattern dots still collide.
         */
        for (unsigned sprite = 0; sprite < visible_count; ++sprite) {
            const SpriteLine *line = &visible[sprite];
            for (int source_x = 0; source_x < size; ++source_x) {
                if (!(line->pattern & (0x8000u >> source_x)))
                    continue;
                for (int magnified_x = 0; magnified_x < scale;
                     ++magnified_x) {
                    int x = line->x + source_x * scale + magnified_x;
                    if ((unsigned)x >= MSX1_VIDEO_W)
                        continue;
                    if (occupied[x])
                        collision = true;
                    occupied[x] = true;
                    if (line->colour && !coloured[x]) {
                        vdp->pixels[y * MSX1_VIDEO_W + x] =
                            palette[line->colour];
                        coloured[x] = true;
                    }
                }
            }
        }
    }

    update_sprite_status(vdp, fifth_sprite, last_sprite, collision);
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
    /*
     * Sprite evaluation takes place during the visible scanlines, before
     * vertical blank raises the F flag.
     */
    vdp_render(vdp);
    vdp->status |= 0x80;
    if (vdp->registers[1] & 0x20)
        vdp->irq = true;
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
    if (vdp->registers[1] & 0x10) {
        render_text(vdp);
        return;
    } else if (vdp->registers[1] & 0x08)
        render_graphics_2(vdp);
    else if (vdp->registers[0] & 0x02)
        render_multicolour(vdp);
    else
        render_graphics_1(vdp);
    render_sprites(vdp);
}
