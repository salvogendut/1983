#include "vdp.h"

#include <string.h>

/*
 * TMS9918A family palette. Colour zero is transparent on sprites and
 * otherwise resolves to the backdrop colour; the renderer handles that
 * choice before indexing this table.
 */
static const u32 tms_palette[MSX_VDP_PALETTE_SIZE] = {
    0x000000, 0x000000, 0x3EB849, 0x74D07D,
    0x5955E0, 0x8076F1, 0xB95E51, 0x65DBEF,
    0xDB6559, 0xFF897D, 0xCCC35E, 0xDED087,
    0x3AA241, 0xB766B5, 0xCCCCCC, 0xFFFFFF,
};

/*
 * V9938 Data Book, appendix 8. Values are encoded as 0GRB, with three
 * significant bits per component.
 */
static const u16 v9938_default_palette[MSX_VDP_PALETTE_SIZE] = {
    0x000, 0x000, 0x611, 0x733,
    0x117, 0x327, 0x151, 0x627,
    0x171, 0x373, 0x661, 0x664,
    0x411, 0x265, 0x555, 0x777,
};

static const u8 tms_register_masks[8] = {
    0x03, 0xfb, 0x0f, 0xff, 0x07, 0x7f, 0x07, 0xff,
};

static const u8 v9938_register_masks[32] = {
    0x7e, 0x7f, 0x7f, 0xff, 0x3f, 0xff, 0x3f, 0xff,
    0xfb, 0xbf, 0x07, 0x03, 0xff, 0xff, 0x07, 0x0f,
    0x0f, 0xbf, 0xff, 0xff, 0x3f, 0x3f, 0x3f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static unsigned vram_size(const MsxVdp *vdp) {
    return vdp->type == MSX_VDP_V9938
         ? MSX2_VRAM_SIZE : MSX1_VRAM_SIZE;
}

static unsigned wrap_address(const MsxVdp *vdp, unsigned address) {
    return address & (vram_size(vdp) - 1);
}

static u8 display_mode(const MsxVdp *vdp) {
    return (u8)(((vdp->registers[0] & 0x0e) << 1) |
                ((vdp->registers[1] & 0x08) >> 2) |
                ((vdp->registers[1] & 0x10) >> 4));
}

static bool planar_vram(const MsxVdp *vdp) {
    u8 mode = display_mode(vdp);
    return vdp->type == MSX_VDP_V9938 &&
           (mode == 0x14 || mode == 0x1c);
}

static unsigned cpu_vram_address(const MsxVdp *vdp) {
    unsigned address = vdp->address;

    if (vdp->type == MSX_VDP_V9938)
        address |= (unsigned)(vdp->registers[14] & 0x07) << 14;
    if (planar_vram(vdp))
        address = ((address << 16) | (address >> 1)) & 0x1ffff;
    return wrap_address(vdp, address);
}

static void increment_vram_pointer(MsxVdp *vdp) {
    vdp->address = (u16)((vdp->address + 1) & 0x3fff);
    if (vdp->address == 0 && vdp->type == MSX_VDP_V9938 &&
        (display_mode(vdp) & 0x18))
        vdp->registers[14] = (vdp->registers[14] + 1) & 0x07;
}

static u8 expand_three_bits(unsigned value) {
    return (u8)((value * 255 + 3) / 7);
}

static u32 palette_colour(const MsxVdp *vdp, u8 index) {
    u16 grb;
    u8 red;
    u8 green;
    u8 blue;

    index &= 0x0f;
    if (vdp->type != MSX_VDP_V9938)
        return tms_palette[index];
    grb = vdp->palette_grb[index];
    red = expand_three_bits((grb >> 4) & 0x07);
    green = expand_three_bits((grb >> 8) & 0x07);
    blue = expand_three_bits(grb & 0x07);
    return ((u32)red << 16) | ((u32)green << 8) | blue;
}

static u8 visible_colour(const MsxVdp *vdp, u8 colour) {
    if ((colour & 0x0f) == 0 &&
        (vdp->type != MSX_VDP_V9938 ||
         !(vdp->registers[8] & 0x20)))
        return vdp->registers[7] & 0x0f;
    return colour & 0x0f;
}

static void put_pixel(MsxVdp *vdp, int x, int y, u8 colour) {
    if ((unsigned)x >= MSX1_VIDEO_W || (unsigned)y >= MSX1_VIDEO_H)
        return;
    vdp->pixels[y * MSX1_VIDEO_W + x] =
        palette_colour(vdp, visible_colour(vdp, colour));
}

typedef struct {
    int x;
    unsigned pattern;
    u8 colour;
} SpriteLine;

static unsigned sprite_pattern_line(const MsxVdp *vdp, u8 pattern_number,
                                    int size, unsigned line) {
    unsigned pattern_mask =
        vdp->type == MSX_VDP_V9938 ? 0x3f : 0x07;
    unsigned pattern_base =
        (unsigned)(vdp->registers[6] & pattern_mask) << 11;
    unsigned left_pattern;
    unsigned row = line & 7;
    unsigned bits;

    if (size == 16)
        pattern_number &= 0xfc;
    left_pattern = pattern_number + (line >= 8 ? 1u : 0u);
    bits = (unsigned)vdp->vram[wrap_address(vdp,
        pattern_base + left_pattern * 8 + row)] << 8;
    if (size == 16) {
        bits |= vdp->vram[wrap_address(vdp,
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
    unsigned attribute_base =
        (unsigned)(vdp->registers[5] &
                   (vdp->type == MSX_VDP_V9938 ? 0xff : 0x7f)) << 7;
    int size = vdp->registers[1] & 0x02 ? 16 : 8;
    int scale = vdp->registers[1] & 0x01 ? 2 : 1;
    int effective_size = size * scale;
    unsigned sprite_end = 0;
    unsigned last_sprite;
    int fifth_sprite = -1;
    bool collision = false;

    if (vdp->type == MSX_VDP_V9938)
        attribute_base |= (unsigned)(vdp->registers[11] & 0x03) << 15;
    while (sprite_end < 32 &&
           vdp->vram[wrap_address(
               vdp, attribute_base + sprite_end * 4)] != 0xd0)
        ++sprite_end;
    last_sprite = sprite_end < 32 ? sprite_end : 31;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        SpriteLine visible[4];
        unsigned visible_count = 0;
        bool occupied[MSX1_VIDEO_W] = { false };
        bool coloured[MSX1_VIDEO_W] = { false };

        for (unsigned sprite = 0; sprite < sprite_end; ++sprite) {
            unsigned offset = wrap_address(
                vdp, attribute_base + sprite * 4);
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

            colour_attribute = vdp->vram[wrap_address(vdp, offset + 3)];
            visible[visible_count].x =
                vdp->vram[wrap_address(vdp, offset + 1)]
                - (colour_attribute & 0x80 ? 32 : 0);
            visible[visible_count].pattern = sprite_pattern_line(
                vdp, vdp->vram[wrap_address(vdp, offset + 2)], size,
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
                            palette_colour(vdp, line->colour);
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
    memcpy(vdp->palette_grb, v9938_default_palette,
           sizeof(vdp->palette_grb));
    if (vdp->type == MSX_VDP_V9938) {
        vdp->registers[21] = 0x3b;
        vdp->registers[22] = 0x05;
    }
    vdp->status = 0;
    vdp->status1 = 0;
    vdp->status2 = vdp->type == MSX_VDP_V9938 ? 0x0c : 0;
    vdp->read_buffer = 0;
    vdp->control_first = 0;
    vdp->address = 0;
    vdp->control_pending = false;
    vdp->palette_pending = false;
    vdp->irq = false;
    memset(vdp->pixels, 0, sizeof(vdp->pixels));
}

void vdp_init(MsxVdp *vdp) {
    if (!vdp)
        return;
    memset(vdp, 0, sizeof(*vdp));
    vdp->type = MSX_VDP_TMS9918;
    vdp_reset(vdp);
}

void vdp_set_type(MsxVdp *vdp, MsxVdpType type) {
    if (!vdp)
        return;
    vdp->type = type == MSX_VDP_V9938
              ? MSX_VDP_V9938 : MSX_VDP_TMS9918;
}

static void write_register(MsxVdp *vdp, unsigned reg, u8 value) {
    if (vdp->type == MSX_VDP_TMS9918) {
        reg &= 0x07;
        value &= tms_register_masks[reg];
    } else if (reg < 32) {
        value &= v9938_register_masks[reg];
    } else if (reg >= 47) {
        return;
    }

    vdp->registers[reg] = value;
    if (reg == 1 && !(value & 0x20))
        vdp->irq = false;
    if (reg == 16)
        vdp->palette_pending = false;
}

u8 vdp_read_data(MsxVdp *vdp) {
    u8 value;

    if (!vdp)
        return 0xff;
    value = vdp->read_buffer;
    vdp->read_buffer = vdp->vram[cpu_vram_address(vdp)];
    increment_vram_pointer(vdp);
    vdp->control_pending = false;
    return value;
}

u8 vdp_read_status(MsxVdp *vdp) {
    unsigned reg;
    u8 value;

    if (!vdp)
        return 0xff;
    reg = vdp->type == MSX_VDP_V9938 ? vdp->registers[15] : 0;
    switch (reg) {
        case 0:
            value = vdp->status;
            vdp->status &= 0x1f;
            vdp->irq = false;
            break;
        case 1:
            value = vdp->status1;
            break;
        case 2:
            value = vdp->status2;
            break;
        case 3:
        case 5:
        case 7:
        case 8:
            value = 0;
            if (reg == 5)
                vdp->status &= (u8)~0x20;
            break;
        case 4:
        case 9:
            value = 0xfe;
            break;
        case 6:
            value = 0xfc;
            break;
        default:
            value = 0xff;
            break;
    }
    vdp->control_pending = false;
    return value;
}

void vdp_write_data(MsxVdp *vdp, u8 value) {
    if (!vdp)
        return;
    vdp->vram[cpu_vram_address(vdp)] = value;
    increment_vram_pointer(vdp);
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
        unsigned reg;

        if (vdp->type == MSX_VDP_V9938 && (value & 0x40))
            return;
        reg = value & (vdp->type == MSX_VDP_V9938 ? 0x3f : 0x07);
        write_register(vdp, reg, vdp->control_first);
        return;
    }

    vdp->address =
        (u16)(((u16)(value & 0x3f) << 8) | vdp->control_first);
    if (!(value & 0x40)) {
        vdp->read_buffer = vdp->vram[cpu_vram_address(vdp)];
        increment_vram_pointer(vdp);
    }
}

void vdp_write_palette(MsxVdp *vdp, u8 value) {
    unsigned index;

    if (!vdp || vdp->type != MSX_VDP_V9938)
        return;
    if (!vdp->palette_pending) {
        vdp->control_first = value;
        vdp->palette_pending = true;
        return;
    }

    index = vdp->registers[16] & 0x0f;
    vdp->palette_grb[index] =
        (u16)(((u16)value << 8) | vdp->control_first) & 0x0777;
    vdp->registers[16] = (u8)((index + 1) & 0x0f);
    vdp->palette_pending = false;
}

void vdp_write_indirect(MsxVdp *vdp, u8 value) {
    u8 selector;

    if (!vdp || vdp->type != MSX_VDP_V9938)
        return;
    vdp->control_first = value;
    selector = vdp->registers[17];
    write_register(vdp, selector & 0x3f, value);
    if (!(selector & 0x80))
        vdp->registers[17] = (selector + 1) & 0x3f;
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
    unsigned name_mask =
        vdp->type == MSX_VDP_V9938 ? 0x7f : 0x0f;
    unsigned pattern_mask =
        vdp->type == MSX_VDP_V9938 ? 0x3f : 0x07;
    unsigned name_base =
        (unsigned)(vdp->registers[2] & name_mask) << 10;
    unsigned colour_base = (unsigned)vdp->registers[3] << 6;
    unsigned pattern_base =
        (unsigned)(vdp->registers[4] & pattern_mask) << 11;

    if (vdp->type == MSX_VDP_V9938)
        colour_base |= (unsigned)(vdp->registers[10] & 0x07) << 14;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int line = y & 7;
        for (int column = 0; column < 32; ++column) {
            u8 name = vdp->vram[wrap_address(vdp,
                name_base + row * 32 + column)];
            u8 pattern = vdp->vram[wrap_address(vdp,
                pattern_base + name * 8 + line)];
            u8 colours = vdp->vram[wrap_address(vdp,
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
    unsigned name_mask =
        vdp->type == MSX_VDP_V9938 ? 0x7f : 0x0f;
    unsigned name_base =
        (unsigned)(vdp->registers[2] & name_mask) << 10;
    unsigned pattern_base =
        (unsigned)(vdp->registers[4] & 0x3c) << 11;
    unsigned colour_base =
        (unsigned)(vdp->registers[3] & 0x80) << 6;

    if (vdp->type == MSX_VDP_V9938)
        colour_base |= (unsigned)(vdp->registers[10] & 0x07) << 14;
    else
        pattern_base = (unsigned)(vdp->registers[4] & 0x04) << 11;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int line = y & 7;
        int third = (y >> 6) * 0x800;
        for (int column = 0; column < 32; ++column) {
            u8 name = vdp->vram[wrap_address(vdp,
                name_base + row * 32 + column)];
            unsigned offset = (unsigned)(third + name * 8 + line);
            u8 pattern = vdp->vram[
                wrap_address(vdp, pattern_base + offset)];
            u8 colours = vdp->vram[
                wrap_address(vdp, colour_base + offset)];
            for (int bit = 0; bit < 8; ++bit) {
                u8 colour = pattern & (0x80 >> bit)
                          ? colours >> 4 : colours & 0x0f;
                put_pixel(vdp, column * 8 + bit, y, colour);
            }
        }
    }
}

static void render_text(MsxVdp *vdp) {
    unsigned name_mask =
        vdp->type == MSX_VDP_V9938 ? 0x7f : 0x0f;
    unsigned pattern_mask =
        vdp->type == MSX_VDP_V9938 ? 0x3f : 0x07;
    unsigned name_base =
        (unsigned)(vdp->registers[2] & name_mask) << 10;
    unsigned pattern_base =
        (unsigned)(vdp->registers[4] & pattern_mask) << 11;
    u8 foreground = vdp->registers[7] >> 4;
    u8 background = vdp->registers[7] & 0x0f;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int line = y & 7;
        for (int column = 0; column < 40; ++column) {
            u8 name = vdp->vram[wrap_address(vdp,
                name_base + row * 40 + column)];
            u8 pattern = vdp->vram[wrap_address(vdp,
                pattern_base + name * 8 + line)];
            for (int bit = 0; bit < 6; ++bit)
                put_pixel(vdp, 8 + column * 6 + bit, y,
                          pattern & (0x80 >> bit)
                          ? foreground : background);
        }
    }
}

static void render_multicolour(MsxVdp *vdp) {
    unsigned name_mask =
        vdp->type == MSX_VDP_V9938 ? 0x7f : 0x0f;
    unsigned pattern_mask =
        vdp->type == MSX_VDP_V9938 ? 0x3f : 0x07;
    unsigned name_base =
        (unsigned)(vdp->registers[2] & name_mask) << 10;
    unsigned pattern_base =
        (unsigned)(vdp->registers[4] & pattern_mask) << 11;

    for (int y = 0; y < MSX1_VIDEO_H; ++y) {
        int row = y >> 3;
        int pattern_line = ((y & 0x1c) >> 2);
        for (int column = 0; column < 32; ++column) {
            u8 name = vdp->vram[wrap_address(vdp,
                name_base + row * 32 + column)];
            u8 colours = vdp->vram[wrap_address(vdp,
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
        vdp->pixels[i] = palette_colour(vdp, backdrop);

    if (!(vdp->registers[1] & 0x40))
        return;
    /*
     * TMS9918A mode bits are M1=R1.4, M2=R1.3, and M3=R0.1.
     * M2 selects Multicolour while the later M3 bit selects Graphics II.
     */
    if (vdp->registers[1] & 0x10) {
        render_text(vdp);
        return;
    } else if (vdp->registers[0] & 0x02)
        render_graphics_2(vdp);
    else if (vdp->registers[1] & 0x08)
        render_multicolour(vdp);
    else
        render_graphics_1(vdp);
    render_sprites(vdp);
}
