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

#define V9938_STATUS2_TR 0x80
#define V9938_STATUS2_BD 0x10
#define V9938_STATUS2_CE 0x01

static unsigned bitmap_address(u8 mode, unsigned x, unsigned y);
static u8 bitmap_pixel(const MsxVdp *vdp, u8 mode,
                       unsigned x, unsigned y);
static void execute_vdp_command(MsxVdp *vdp);
static void command_transfer_write(MsxVdp *vdp);
static u8 command_read_colour(MsxVdp *vdp);

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
    if ((unsigned)x >= vdp->render_width ||
        (unsigned)y >= vdp->render_height)
        return;
    vdp->pixels[y * vdp->render_width + x] =
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
    vdp->status7 = 0;
    vdp->read_buffer = 0;
    vdp->control_first = 0;
    vdp->address = 0;
    vdp->control_pending = false;
    vdp->palette_pending = false;
    vdp->irq = false;
    vdp->command_x = 0;
    vdp->command_y = 0;
    vdp->command_origin_x = 0;
    vdp->command_row_length = 0;
    vdp->command_remaining_x = 0;
    vdp->command_remaining_y = 0;
    vdp->command_border_x = 0;
    vdp->command_code = 0;
    vdp->command_mode = 0;
    vdp->command_argument = 0;
    vdp->render_width = MSX1_VIDEO_W;
    vdp->render_height = MSX1_VIDEO_H;
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
    if (vdp->type == MSX_VDP_V9938 && reg == 44)
        command_transfer_write(vdp);
    if (vdp->type == MSX_VDP_V9938 && reg == 46)
        execute_vdp_command(vdp);
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
            value = 0;
            if (reg == 5)
                vdp->status &= (u8)~0x20;
            break;
        case 7:
            value = command_read_colour(vdp);
            break;
        case 8:
            value = (u8)vdp->command_border_x;
            break;
        case 4:
            value = 0xfe;
            break;
        case 6:
            value = 0xfc;
            break;
        case 9:
            value = (u8)((vdp->command_border_x >> 8) | 0xfe);
            vdp->status2 &= (u8)~V9938_STATUS2_BD;
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

static unsigned bitmap_page_y(const MsxVdp *vdp, u8 mode) {
    if (mode == 0x0c || mode == 0x10)
        return (unsigned)(vdp->registers[2] & 0x60) << 3;
    return (unsigned)(vdp->registers[2] & 0x20) << 3;
}

static unsigned bitmap_address(u8 mode, unsigned x, unsigned y) {
    switch (mode) {
        case 0x0c: /* SCREEN 5: 256 pixels, two 4-bit pixels per byte. */
            return ((y & 1023) << 7) | ((x & 255) >> 1);
        case 0x10: /* SCREEN 6: 512 pixels, four 2-bit pixels per byte. */
            return ((y & 1023) << 7) | ((x & 511) >> 2);
        case 0x14: /* SCREEN 7: 512 pixels, planar 4-bit pixels. */
            return ((x & 2) << 15) |
                   ((y & 511) << 7) | ((x & 511) >> 2);
        case 0x1c: /* SCREEN 8: 256 pixels, planar 8-bit pixels. */
            return ((x & 1) << 16) |
                   ((y & 511) << 7) | ((x & 255) >> 1);
        default:
            return 0;
    }
}

static u8 bitmap_pixel(const MsxVdp *vdp, u8 mode,
                       unsigned x, unsigned y) {
    u8 packed = vdp->vram[
        wrap_address(vdp, bitmap_address(mode, x, y))];

    switch (mode) {
        case 0x0c:
        case 0x14:
            return (packed >> (((~x) & 1) << 2)) & 0x0f;
        case 0x10:
            return (packed >> (((~x) & 3) << 1)) & 0x03;
        case 0x1c:
            return packed;
        default:
            return 0;
    }
}

static bool command_mode_info(u8 mode, unsigned *width,
                              unsigned *colour_mask,
                              unsigned *pixels_per_byte) {
    switch (mode) {
        case 0x0c:
            *width = 256;
            *colour_mask = 0x0f;
            *pixels_per_byte = 2;
            return true;
        case 0x10:
            *width = 512;
            *colour_mask = 0x03;
            *pixels_per_byte = 4;
            return true;
        case 0x14:
            *width = 512;
            *colour_mask = 0x0f;
            *pixels_per_byte = 2;
            return true;
        case 0x1c:
            *width = 256;
            *colour_mask = 0xff;
            *pixels_per_byte = 1;
            return true;
        default:
            return false;
    }
}

static unsigned command_coordinate(const MsxVdp *vdp, unsigned low_reg,
                                   unsigned high_mask) {
    return vdp->registers[low_reg] |
           ((unsigned)(vdp->registers[low_reg + 1] & high_mask) << 8);
}

static unsigned command_clip_x1(unsigned x, unsigned count,
                                unsigned width, bool reverse,
                                unsigned pixels_per_byte,
                                bool byte_command) {
    unsigned units = width;
    unsigned position = x;

    if (byte_command) {
        units /= pixels_per_byte;
        position /= pixels_per_byte;
        count /= pixels_per_byte;
    }
    if (position >= units)
        return 1;
    if (!count)
        count = units;
    return reverse
         ? (count < position + 1 ? count : position + 1)
         : (count < units - position ? count : units - position);
}

static unsigned command_clip_x2(unsigned sx, unsigned dx, unsigned count,
                                unsigned width, bool reverse,
                                unsigned pixels_per_byte,
                                bool byte_command) {
    unsigned units = width;
    unsigned source = sx;
    unsigned destination = dx;
    unsigned boundary;

    if (byte_command) {
        units /= pixels_per_byte;
        source /= pixels_per_byte;
        destination /= pixels_per_byte;
        count /= pixels_per_byte;
    }
    if (source >= units || destination >= units)
        return 1;
    if (!count)
        count = units;
    boundary = reverse
             ? (source < destination ? source : destination) + 1
             : units - (source > destination ? source : destination);
    return count < boundary ? count : boundary;
}

static unsigned command_clip_y1(unsigned y, unsigned count,
                                bool reverse) {
    if (!count)
        count = 1024;
    return reverse && count > y + 1 ? y + 1 : count;
}

static unsigned command_clip_y2(unsigned sy, unsigned dy, unsigned count,
                                bool reverse) {
    unsigned boundary;

    if (!count)
        count = 1024;
    if (!reverse)
        return count;
    boundary = (sy < dy ? sy : dy) + 1;
    return count < boundary ? count : boundary;
}

static void command_write_pixel(MsxVdp *vdp, u8 mode,
                                unsigned x, unsigned y, u8 colour,
                                u8 operation) {
    unsigned width;
    unsigned colour_mask;
    unsigned pixels_per_byte;
    unsigned address;
    unsigned shift;
    u8 old_colour;
    u8 new_colour;
    u8 packed_mask;

    if (!command_mode_info(mode, &width, &colour_mask,
                           &pixels_per_byte))
        return;
    (void)width;
    (void)pixels_per_byte;
    colour &= (u8)colour_mask;
    if ((operation & 0x08) && !colour)
        return;
    old_colour = bitmap_pixel(vdp, mode, x, y);
    switch (operation & 0x07) {
        case 0:
            new_colour = colour;
            break;
        case 1:
            new_colour = old_colour & colour;
            break;
        case 2:
            new_colour = old_colour | colour;
            break;
        case 3:
            new_colour = old_colour ^ colour;
            break;
        case 4:
            new_colour = (u8)(~colour & colour_mask);
            break;
        default:
            return;
    }

    address = wrap_address(vdp, bitmap_address(mode, x, y));
    if (mode == 0x1c) {
        vdp->vram[address] = new_colour;
        return;
    }
    if (mode == 0x10)
        shift = ((~x) & 3) << 1;
    else
        shift = ((~x) & 1) << 2;
    packed_mask = (u8)(colour_mask << shift);
    vdp->vram[address] =
        (u8)((vdp->vram[address] & ~packed_mask) |
             ((new_colour << shift) & packed_mask));
}

static void command_complete(MsxVdp *vdp) {
    vdp->status2 &= (u8)~(V9938_STATUS2_TR | V9938_STATUS2_CE);
    vdp->registers[46] = 0;
    vdp->command_code = 0;
}

static bool command_advance_transfer(MsxVdp *vdp,
                                     unsigned pixels_per_byte) {
    int x_step = vdp->command_code == 0x0f
               ? (int)pixels_per_byte : 1;
    int y_step = vdp->command_argument & 0x08 ? -1 : 1;

    if (vdp->command_argument & 0x04)
        x_step = -x_step;
    if (--vdp->command_remaining_x) {
        vdp->command_x = (u16)(vdp->command_x + x_step);
        return true;
    }
    if (!--vdp->command_remaining_y) {
        command_complete(vdp);
        return false;
    }
    vdp->command_x = vdp->command_origin_x;
    vdp->command_y = (u16)(vdp->command_y + y_step);
    vdp->command_remaining_x = vdp->command_row_length;
    return true;
}

static void command_load_colour(MsxVdp *vdp) {
    if (vdp->command_argument & 0x10)
        vdp->status7 = 0xff;
    else
        vdp->status7 = bitmap_pixel(
            vdp, vdp->command_mode, vdp->command_x, vdp->command_y);
    vdp->registers[44] = vdp->status7;
}

static u8 command_read_colour(MsxVdp *vdp) {
    u8 value = vdp->status7;

    if (vdp->command_code == 0x0a &&
        (vdp->status2 & V9938_STATUS2_CE)) {
        unsigned width = 0;
        unsigned colour_mask = 0;
        unsigned pixels_per_byte = 1;

        command_mode_info(vdp->command_mode, &width, &colour_mask,
                          &pixels_per_byte);
        (void)width;
        (void)colour_mask;
        if (command_advance_transfer(vdp, pixels_per_byte))
            command_load_colour(vdp);
    } else if (!(vdp->status2 & V9938_STATUS2_CE)) {
        vdp->status2 &= (u8)~V9938_STATUS2_TR;
    }
    return value;
}

static void command_transfer_write(MsxVdp *vdp) {
    unsigned width;
    unsigned colour_mask;
    unsigned pixels_per_byte;

    if (!(vdp->status2 & V9938_STATUS2_CE) ||
        !(vdp->status2 & V9938_STATUS2_TR) ||
        (vdp->command_code != 0x0b &&
         vdp->command_code != 0x0f) ||
        !command_mode_info(vdp->command_mode, &width, &colour_mask,
                           &pixels_per_byte))
        return;
    (void)width;
    (void)colour_mask;
    if (!(vdp->command_argument & 0x20)) {
        if (vdp->command_code == 0x0b) {
            command_write_pixel(
                vdp, vdp->command_mode, vdp->command_x, vdp->command_y,
                vdp->registers[44], vdp->registers[46] & 0x0f);
        } else {
            unsigned address = wrap_address(vdp, bitmap_address(
                vdp->command_mode, vdp->command_x, vdp->command_y));
            vdp->vram[address] = vdp->registers[44];
        }
    }
    command_advance_transfer(vdp, pixels_per_byte);
}

static void command_setup_transfer(MsxVdp *vdp, u8 code, u8 mode,
                                   unsigned x, unsigned y,
                                   unsigned row_length,
                                   unsigned rows) {
    vdp->command_code = code;
    vdp->command_mode = mode;
    vdp->command_argument = vdp->registers[45];
    vdp->command_x = (u16)x;
    vdp->command_y = (u16)y;
    vdp->command_origin_x = (u16)x;
    vdp->command_row_length = (u16)row_length;
    vdp->command_remaining_x = (u16)row_length;
    vdp->command_remaining_y = (u16)rows;
    vdp->status2 |= V9938_STATUS2_TR;
}

static void execute_vdp_command(MsxVdp *vdp) {
    u8 command = vdp->registers[46];
    u8 code = command >> 4;
    u8 mode = display_mode(vdp);
    u8 argument = vdp->registers[45];
    bool reverse_x = (argument & 0x04) != 0;
    bool reverse_y = (argument & 0x08) != 0;
    bool source_external = (argument & 0x10) != 0;
    bool destination_external = (argument & 0x20) != 0;
    unsigned width;
    unsigned colour_mask;
    unsigned pixels_per_byte;
    unsigned sx = command_coordinate(vdp, 32, 0x01);
    unsigned sy = command_coordinate(vdp, 34, 0x03);
    unsigned dx = command_coordinate(vdp, 36, 0x01);
    unsigned dy = command_coordinate(vdp, 38, 0x03);
    unsigned nx = command_coordinate(vdp, 40, 0x03);
    unsigned ny = command_coordinate(vdp, 42, 0x03);
    int tx = reverse_x ? -1 : 1;
    int ty = reverse_y ? -1 : 1;

    vdp->status2 &= (u8)~V9938_STATUS2_TR;
    if (code < 4 ||
        !command_mode_info(mode, &width, &colour_mask,
                           &pixels_per_byte)) {
        command_complete(vdp);
        return;
    }
    vdp->status2 |= V9938_STATUS2_CE;
    vdp->command_code = code;
    vdp->command_mode = mode;
    vdp->command_argument = argument;

    switch (code) {
        case 0x04: /* POINT */
            vdp->status7 = source_external
                         ? 0xff : bitmap_pixel(vdp, mode, sx, sy);
            vdp->registers[44] = vdp->status7;
            command_complete(vdp);
            break;

        case 0x05: /* PSET */
            if (!destination_external)
                command_write_pixel(vdp, mode, dx, dy,
                                    vdp->registers[44],
                                    command & 0x0f);
            command_complete(vdp);
            break;

        case 0x06: { /* SRCH */
            int x = (int)sx;
            u8 colour = vdp->registers[44] & (u8)colour_mask;
            bool find_unequal = (argument & 0x02) != 0;
            bool outside_at_start = sx >= width;

            for (;;) {
                u8 found = source_external
                         ? 0xff
                         : bitmap_pixel(vdp, mode, (unsigned)x, sy);
                if ((found == colour) != find_unequal) {
                    vdp->status2 |= V9938_STATUS2_BD;
                    break;
                }
                x += tx;
                if (outside_at_start ||
                    x < 0 || x >= (int)width)
                    break;
            }
            vdp->command_border_x = (u16)x;
            command_complete(vdp);
            break;
        }

        case 0x07: { /* LINE */
            unsigned major = nx & 0x03ff;
            unsigned minor = ny & 0x03ff;
            unsigned error = major ? (major - 1) >> 1 : 0;
            int x = (int)dx;
            int y = (int)dy;

            for (unsigned step = 0; step <= major; ++step) {
                if (!destination_external && y >= 0)
                    command_write_pixel(
                        vdp, mode, (unsigned)x, (unsigned)y,
                        vdp->registers[44], command & 0x0f);
                if (step == major)
                    break;
                if (!(argument & 0x01)) {
                    x += tx;
                    if (error < minor) {
                        error += major;
                        y += ty;
                    }
                } else {
                    y += ty;
                    if (error < minor) {
                        error += major;
                        x += tx;
                    }
                }
                error = (error - minor) & 0x03ff;
                if (x < 0 || x >= (int)width || y < 0)
                    break;
            }
            command_complete(vdp);
            break;
        }

        case 0x08: { /* LMMV */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, false);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);
            unsigned y = dy;

            for (unsigned row = 0; row < rows; ++row) {
                unsigned x = dx;
                for (unsigned column = 0; column < columns; ++column) {
                    if (!destination_external)
                        command_write_pixel(
                            vdp, mode, x, y, vdp->registers[44],
                            command & 0x0f);
                    x = (unsigned)((int)x + tx);
                }
                y = (unsigned)((int)y + ty);
            }
            command_complete(vdp);
            break;
        }

        case 0x09: { /* LMMM */
            unsigned columns = command_clip_x2(
                sx, dx, nx, width, reverse_x, pixels_per_byte, false);
            unsigned rows = command_clip_y2(sy, dy, ny, reverse_y);
            unsigned source_y = sy;
            unsigned destination_y = dy;

            for (unsigned row = 0; row < rows; ++row) {
                unsigned source_x = sx;
                unsigned destination_x = dx;
                for (unsigned column = 0; column < columns; ++column) {
                    u8 colour = source_external
                              ? 0xff
                              : bitmap_pixel(vdp, mode, source_x,
                                             source_y);
                    if (!destination_external)
                        command_write_pixel(
                            vdp, mode, destination_x, destination_y,
                            colour, command & 0x0f);
                    source_x = (unsigned)((int)source_x + tx);
                    destination_x =
                        (unsigned)((int)destination_x + tx);
                }
                source_y = (unsigned)((int)source_y + ty);
                destination_y =
                    (unsigned)((int)destination_y + ty);
            }
            command_complete(vdp);
            break;
        }

        case 0x0a: { /* LMCM: logical VRAM-to-CPU transfer. */
            unsigned columns = command_clip_x1(
                sx, nx, width, reverse_x, pixels_per_byte, false);
            unsigned rows = command_clip_y1(sy, ny, reverse_y);

            command_setup_transfer(
                vdp, code, mode, sx, sy, columns, rows);
            command_load_colour(vdp);
            break;
        }

        case 0x0b: { /* LMMC: logical CPU-to-VRAM transfer. */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, false);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);

            command_setup_transfer(
                vdp, code, mode, dx, dy, columns, rows);
            break;
        }

        case 0x0c: { /* HMMV */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);
            int byte_step = reverse_x
                          ? -(int)pixels_per_byte
                          : (int)pixels_per_byte;
            unsigned y = dy;

            for (unsigned row = 0; row < rows; ++row) {
                unsigned x = dx;
                for (unsigned column = 0; column < columns; ++column) {
                    if (!destination_external) {
                        unsigned address = wrap_address(
                            vdp, bitmap_address(mode, x, y));
                        vdp->vram[address] = vdp->registers[44];
                    }
                    x = (unsigned)((int)x + byte_step);
                }
                y = (unsigned)((int)y + ty);
            }
            command_complete(vdp);
            break;
        }

        case 0x0d: { /* HMMM */
            unsigned columns = command_clip_x2(
                sx, dx, nx, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y2(sy, dy, ny, reverse_y);
            int byte_step = reverse_x
                          ? -(int)pixels_per_byte
                          : (int)pixels_per_byte;
            unsigned source_y = sy;
            unsigned destination_y = dy;

            for (unsigned row = 0; row < rows; ++row) {
                unsigned source_x = sx;
                unsigned destination_x = dx;
                for (unsigned column = 0; column < columns; ++column) {
                    u8 value = source_external
                             ? 0xff
                             : vdp->vram[wrap_address(
                                 vdp, bitmap_address(
                                     mode, source_x, source_y))];
                    if (!destination_external)
                        vdp->vram[wrap_address(
                            vdp, bitmap_address(
                                mode, destination_x,
                                destination_y))] = value;
                    source_x =
                        (unsigned)((int)source_x + byte_step);
                    destination_x =
                        (unsigned)((int)destination_x + byte_step);
                }
                source_y = (unsigned)((int)source_y + ty);
                destination_y =
                    (unsigned)((int)destination_y + ty);
            }
            command_complete(vdp);
            break;
        }

        case 0x0e: { /* YMMM */
            unsigned columns = command_clip_x1(
                dx, 512, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y2(sy, dy, ny, reverse_y);
            int byte_step = reverse_x
                          ? -(int)pixels_per_byte
                          : (int)pixels_per_byte;
            unsigned source_y = sy;
            unsigned destination_y = dy;

            for (unsigned row = 0; row < rows; ++row) {
                unsigned x = dx;
                for (unsigned column = 0; column < columns; ++column) {
                    unsigned source_address = wrap_address(
                        vdp, bitmap_address(mode, x, source_y));
                    unsigned destination_address = wrap_address(
                        vdp, bitmap_address(mode, x, destination_y));
                    if (!destination_external)
                        vdp->vram[destination_address] =
                            vdp->vram[source_address];
                    x = (unsigned)((int)x + byte_step);
                }
                source_y = (unsigned)((int)source_y + ty);
                destination_y =
                    (unsigned)((int)destination_y + ty);
            }
            command_complete(vdp);
            break;
        }

        case 0x0f: { /* HMMC: high-speed CPU-to-VRAM transfer. */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);

            command_setup_transfer(
                vdp, code, mode, dx, dy, columns, rows);
            break;
        }

        default:
            command_complete(vdp);
            break;
    }
}

static u32 screen8_colour(u8 colour) {
    u8 red = expand_three_bits((colour >> 2) & 0x07);
    u8 green = expand_three_bits((colour >> 5) & 0x07);
    unsigned blue_bits = colour & 0x03;
    u8 blue = expand_three_bits(
        blue_bits == 3 ? 7 : blue_bits * 2);

    return ((u32)red << 16) | ((u32)green << 8) | blue;
}

static void render_bitmap(MsxVdp *vdp, u8 mode) {
    unsigned page_y = bitmap_page_y(vdp, mode);
    unsigned vertical_scroll = vdp->registers[23];

    for (unsigned y = 0; y < vdp->render_height; ++y) {
        unsigned source_y = page_y + ((y + vertical_scroll) & 0xff);
        for (unsigned x = 0; x < vdp->render_width; ++x) {
            u8 colour = bitmap_pixel(vdp, mode, x, source_y);
            u32 rgb;

            if (mode == 0x1c) {
                rgb = screen8_colour(colour);
            } else if (mode == 0x10 && colour == 0 &&
                       !(vdp->registers[8] & 0x20)) {
                u8 backdrop = x & 1
                            ? vdp->registers[7] & 0x03
                            : (vdp->registers[7] >> 2) & 0x03;
                rgb = palette_colour(vdp, backdrop);
            } else {
                rgb = palette_colour(
                    vdp, visible_colour(vdp, colour));
            }
            vdp->pixels[y * vdp->render_width + x] = rgb;
        }
    }
}

void vdp_render(MsxVdp *vdp) {
    u8 backdrop;
    u8 mode;

    if (!vdp)
        return;
    mode = display_mode(vdp);
    if (vdp->type == MSX_VDP_V9938 &&
        (mode == 0x0c || mode == 0x10 ||
         mode == 0x14 || mode == 0x1c)) {
        vdp->render_width =
            mode == 0x10 || mode == 0x14
            ? MSX2_VIDEO_W : MSX1_VIDEO_W;
        vdp->render_height =
            vdp->registers[9] & 0x80
            ? MSX2_VIDEO_H : MSX1_VIDEO_H;
    } else {
        vdp->render_width = MSX1_VIDEO_W;
        vdp->render_height = MSX1_VIDEO_H;
    }
    backdrop = vdp->registers[7] & 0x0f;
    for (size_t i = 0;
         i < vdp->render_width * vdp->render_height; ++i)
        vdp->pixels[i] = palette_colour(vdp, backdrop);

    if (!(vdp->registers[1] & 0x40))
        return;
    if (vdp->type == MSX_VDP_V9938 &&
        (mode == 0x0c || mode == 0x10 ||
         mode == 0x14 || mode == 0x1c)) {
        render_bitmap(vdp, mode);
        return;
    }
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
