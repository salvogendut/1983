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

enum {
    V9938_TICKS_PER_LINE = 1368,
    V9938_SYNC_AND_TOP_ERASE_LINES = 16,
    V9938_PAL_TOP_BORDER_LINES = 36,
    V9938_NTSC_TOP_BORDER_LINES = 9,
    V9938_LEFT_BORDER_TICKS = 202,
};

enum {
    V9938_COMMAND_EVENT_NONE = 0,
    V9938_COMMAND_EVENT_COMPLETE,
    V9938_COMMAND_EVENT_TRANSFER_READY,
    V9938_COMMAND_EVENT_STEP,
};

/*
 * Free V9938 VRAM access positions measured on real hardware. These are the
 * bitmap-mode tables used by openMSX 21.0 and the accompanying VDP timing
 * articles. Values are 21 MHz VDP ticks within a 1368-tick scanline.
 */
static const u16 v9938_slots_screen_off[] = {
       0,    8,   16,   24,   32,   40,   48,   56,   64,   72,
      80,   88,   96,  104,  112,  120,  164,  172,  180,  188,
     196,  204,  212,  220,  228,  236,  244,  252,  260,  268,
     276,  292,  300,  308,  316,  324,  332,  340,  348,  356,
     364,  372,  380,  388,  396,  404,  420,  428,  436,  444,
     452,  460,  468,  476,  484,  492,  500,  508,  516,  524,
     532,  548,  556,  564,  572,  580,  588,  596,  604,  612,
     620,  628,  636,  644,  652,  660,  676,  684,  692,  700,
     708,  716,  724,  732,  740,  748,  756,  764,  772,  780,
     788,  804,  812,  820,  828,  836,  844,  852,  860,  868,
     876,  884,  892,  900,  908,  916,  932,  940,  948,  956,
     964,  972,  980,  988,  996, 1004, 1012, 1020, 1028, 1036,
    1044, 1060, 1068, 1076, 1084, 1092, 1100, 1108, 1116, 1124,
    1132, 1140, 1148, 1156, 1164, 1172, 1188, 1196, 1204, 1212,
    1220, 1228, 1268, 1276, 1284, 1292, 1300, 1308, 1316, 1324,
    1334, 1344, 1352, 1360,
};

static const u16 v9938_slots_sprites_off[] = {
       6,   14,   22,   30,   38,   46,   54,   62,   70,   78,
      86,   94,  102,  110,  118,  162,  170,  182,  188,  214,
     220,  246,  252,  278,  310,  316,  342,  348,  374,  380,
     406,  438,  444,  470,  476,  502,  508,  534,  566,  572,
     598,  604,  630,  636,  662,  694,  700,  726,  732,  758,
     764,  790,  822,  828,  854,  860,  886,  892,  918,  950,
     956,  982,  988, 1014, 1020, 1046, 1078, 1084, 1110, 1116,
    1142, 1148, 1174, 1206, 1212, 1266, 1274, 1282, 1290, 1298,
    1306, 1314, 1322, 1332, 1342, 1350, 1358, 1366,
};

static const u16 v9938_slots_sprites_on[] = {
      28,   92,  162,  170,  188,  220,  252,  316,  348,  380,
     444,  476,  508,  572,  604,  636,  700,  732,  764,  828,
     860,  892,  956,  988, 1020, 1084, 1116, 1148, 1212, 1264,
    1330,
};

static unsigned bitmap_address(u8 mode, unsigned x, unsigned y);
static u8 bitmap_pixel(const MsxVdp *vdp, u8 mode,
                       unsigned x, unsigned y);
static void execute_vdp_command(MsxVdp *vdp);
static void command_transfer_write(MsxVdp *vdp);
static u8 command_read_colour(MsxVdp *vdp);
static void command_engine_advance(MsxVdp *vdp, u64 ticks);
static void command_execute_step(MsxVdp *vdp);
static bool command_has_clock(const MsxVdp *vdp);
static u16 command_current_slot_phase(const MsxVdp *vdp);

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

static unsigned v9938_vertical_adjust(const MsxVdp *vdp) {
    return (vdp->registers[18] >> 4) ^ 7;
}

static unsigned v9938_line_zero(const MsxVdp *vdp) {
    return V9938_SYNC_AND_TOP_ERASE_LINES +
        (vdp->timing_scanlines == 313
         ? V9938_PAL_TOP_BORDER_LINES
         : V9938_NTSC_TOP_BORDER_LINES) +
        (vdp->registers[9] & 0x80 ? 0 : 10) +
        v9938_vertical_adjust(vdp);
}

static unsigned v9938_right_border(const MsxVdp *vdp) {
    bool text_mode = (vdp->registers[1] & 0x10) != 0;
    int horizontal_adjust = (vdp->registers[18] & 0x0f) ^ 7;

    return (unsigned)(
        258 + (horizontal_adjust - 7) * 4 +
        (text_mode ? 36 : 0) +
        (text_mode ? 960 : 1024));
}

/*
 * The V9938 line counter starts at display line zero, is offset by R#23,
 * and matches R#19 at the start of the matching line's right border.
 * Large counter values can carry into the following frame, but the match
 * disappears once the counter is reset in that frame's top border.
 */
static bool v9938_hscan_tick(const MsxVdp *vdp, u64 *tick) {
    u64 frame_ticks;
    u64 match_tick;

    if (vdp->type != MSX_VDP_V9938 ||
        !vdp->timing_frame_cycles || !vdp->timing_scanlines)
        return false;

    frame_ticks =
        (u64)vdp->timing_scanlines * V9938_TICKS_PER_LINE;
    match_tick =
        (u64)v9938_line_zero(vdp) * V9938_TICKS_PER_LINE +
        (u64)((vdp->registers[19] - vdp->registers[23]) & 0xff) *
            V9938_TICKS_PER_LINE +
        v9938_right_border(vdp);
    if (match_tick >= frame_ticks) {
        u64 counter_reset_tick =
            (u64)(8 + v9938_vertical_adjust(vdp)) *
            V9938_TICKS_PER_LINE;

        match_tick -= frame_ticks;
        if (match_tick >= counter_reset_tick)
            return false;
    }
    *tick = match_tick;
    return true;
}

static void update_irq(MsxVdp *vdp) {
    bool vertical =
        (vdp->status & 0x80) && (vdp->registers[1] & 0x20);
    bool horizontal =
        vdp->type == MSX_VDP_V9938 &&
        (vdp->status1 & 0x01) && (vdp->registers[0] & 0x10);

    vdp->irq = vertical || horizontal;
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
    u8 attribute;
} SpriteLine;

static unsigned v9938_table_address(unsigned base_mask, unsigned index,
                                    unsigned index_mask, bool planar) {
    unsigned selected =
        ((MSX2_VRAM_SIZE - 1) & ~index_mask) |
        (index & index_mask);

    if (planar) {
        base_mask =
            ((base_mask << 16) | (base_mask >> 1)) &
            (MSX2_VRAM_SIZE - 1);
        selected =
            ((selected & 1) << 16) |
            ((selected & 0x1fffe) >> 1);
    }
    return base_mask & selected;
}

static u8 sprite_pattern_byte(const MsxVdp *vdp, unsigned index,
                              bool planar) {
    unsigned pattern_mask =
        vdp->type == MSX_VDP_V9938 ? 0x3f : 0x07;
    unsigned pattern_base =
        (unsigned)(vdp->registers[6] & pattern_mask) << 11;
    unsigned address;

    if (vdp->type == MSX_VDP_V9938) {
        address = v9938_table_address(
            pattern_base | 0x7ff, index, 0x7ff, planar);
    } else {
        address = pattern_base + index;
    }
    return vdp->vram[wrap_address(vdp, address)];
}

static unsigned sprite_pattern_line(const MsxVdp *vdp, u8 pattern_number,
                                    int size, unsigned line,
                                    bool planar) {
    unsigned left_pattern;
    unsigned row = line & 7;
    unsigned bits;

    if (size == 16)
        pattern_number &= 0xfc;
    left_pattern = pattern_number + (line >= 8 ? 1u : 0u);
    bits = (unsigned)sprite_pattern_byte(
        vdp, left_pattern * 8 + row, planar) << 8;
    if (size == 16)
        bits |= sprite_pattern_byte(
            vdp, (left_pattern + 2) * 8 + row, planar);
    return bits;
}

static void update_sprite_status(MsxVdp *vdp, int overflow_sprite,
                                 unsigned last_sprite, bool collision) {
    u8 status = vdp->status;

    if (overflow_sprite >= 0) {
        /*
         * Fifth/ninth-sprite detection only latches while both the
         * vertical-blank and overflow flags are clear.
         */
        if (!(status & 0xc0))
            status = (status & 0xa0) | 0x40 | (u8)overflow_sprite;
    } else if (!(status & 0x40)) {
        status = (status & 0xa0) | (u8)(last_sprite & 0x1f);
    }
    if (collision)
        status |= 0x20;
    vdp->status = status;
}

static void latch_sprite_collision(MsxVdp *vdp, int x, int y,
                                    bool *collision) {
    if (!*collision && !(vdp->status & 0x20) &&
        (vdp->type != MSX_VDP_V9938 ||
         !(vdp->registers[8] & 0xc0))) {
        vdp->sprite_collision_x = (u16)(x + 12);
        vdp->sprite_collision_y = (u16)(y + 8);
    }
    *collision = true;
}

static bool sprite_dot(const SpriteLine *line, int size, int scale, int x) {
    int relative = x - line->x;
    int source_x;

    if (relative < 0 || relative >= size * scale)
        return false;
    source_x = relative / scale;
    return (line->pattern & (0x8000u >> source_x)) != 0;
}

static void render_sprites_mode1(MsxVdp *vdp) {
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
        unsigned display_y =
            ((unsigned)y +
             (vdp->type == MSX_VDP_V9938
              ? vdp->registers[23] : 0)) & 0xff;

        for (unsigned sprite = 0; sprite < sprite_end; ++sprite) {
            unsigned offset = wrap_address(
                vdp, attribute_base + sprite * 4);
            u8 raw_y = vdp->vram[offset];
            unsigned top = ((unsigned)raw_y + 1) & 0xff;
            unsigned sprite_line = (display_y + 256 - top) & 0xff;
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
                sprite_line / (unsigned)scale, false);
            visible[visible_count].attribute = colour_attribute;
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
            u8 colour = line->attribute & 0x0f;
            bool colour_zero_opaque =
                vdp->type == MSX_VDP_V9938 &&
                (vdp->registers[8] & 0x20);

            for (int source_x = 0; source_x < size; ++source_x) {
                if (!(line->pattern & (0x8000u >> source_x)))
                    continue;
                for (int magnified_x = 0; magnified_x < scale;
                     ++magnified_x) {
                    int x = line->x + source_x * scale + magnified_x;
                    if ((unsigned)x >= MSX1_VIDEO_W)
                        continue;
                    if (vdp->type != MSX_VDP_V9938 ||
                        colour || colour_zero_opaque) {
                        if (occupied[x])
                            latch_sprite_collision(
                                vdp, x, y, &collision);
                        occupied[x] = true;
                    }
                    if ((colour || colour_zero_opaque) &&
                        !coloured[x]) {
                        vdp->pixels[y * MSX1_VIDEO_W + x] =
                            palette_colour(vdp, colour);
                        coloured[x] = true;
                    }
                }
            }
        }
    }

    update_sprite_status(vdp, fifth_sprite, last_sprite, collision);
}

static unsigned sprite2_attribute_address(const MsxVdp *vdp,
                                          unsigned index,
                                          bool planar) {
    unsigned base_mask =
        ((unsigned)(vdp->registers[11] & 0x03) << 15) |
        ((unsigned)vdp->registers[5] << 7) | 0x7f;

    return v9938_table_address(
        base_mask, index, 0x3ff, planar);
}

static void draw_sprite2_pixel(MsxVdp *vdp, u8 mode,
                               int x, int y, u8 colour) {
    static const u16 screen8_sprite_grb[MSX_VDP_PALETTE_SIZE] = {
        0x000, 0x002, 0x030, 0x032,
        0x300, 0x302, 0x330, 0x332,
        0x472, 0x007, 0x070, 0x077,
        0x700, 0x707, 0x770, 0x777,
    };
    unsigned offset;
    u32 rgb;

    if ((unsigned)x >= MSX1_VIDEO_W ||
        (unsigned)y >= vdp->render_height)
        return;
    offset = (unsigned)y * vdp->render_width;
    if (mode == 0x10) {
        vdp->pixels[offset + (unsigned)x * 2] =
            palette_colour(vdp, colour >> 2);
        vdp->pixels[offset + (unsigned)x * 2 + 1] =
            palette_colour(vdp, colour & 0x03);
    } else if (mode == 0x14) {
        vdp->pixels[offset + (unsigned)x * 2] =
            palette_colour(vdp, colour);
        vdp->pixels[offset + (unsigned)x * 2 + 1] =
            palette_colour(vdp, colour);
    } else if (mode == 0x1c) {
        u16 grb = screen8_sprite_grb[colour & 0x0f];

        rgb = ((u32)expand_three_bits((grb >> 4) & 0x07) << 16) |
              ((u32)expand_three_bits((grb >> 8) & 0x07) << 8) |
              expand_three_bits(grb & 0x07);
        vdp->pixels[offset + (unsigned)x] = rgb;
    } else {
        vdp->pixels[offset + (unsigned)x] =
            palette_colour(vdp, colour);
    }
}

static void render_sprites_mode2(MsxVdp *vdp, u8 mode) {
    bool planar = mode == 0x14 || mode == 0x1c;
    int size = vdp->registers[1] & 0x02 ? 16 : 8;
    int scale = vdp->registers[1] & 0x01 ? 2 : 1;
    int effective_size = size * scale;
    unsigned sprite_end = 0;
    unsigned last_sprite;
    int ninth_sprite = -1;
    bool collision = false;

    while (sprite_end < 32 &&
           vdp->vram[sprite2_attribute_address(
               vdp, 512 + sprite_end * 4, planar)] != 0xd8)
        ++sprite_end;
    last_sprite = sprite_end < 32 ? sprite_end : 31;

    for (unsigned y = 0; y < vdp->render_height; ++y) {
        SpriteLine visible[8];
        unsigned visible_count = 0;
        bool occupied[MSX1_VIDEO_W] = { false };
        unsigned display_y =
            (y + vdp->registers[23]) & 0xff;

        for (unsigned sprite = 0; sprite < sprite_end; ++sprite) {
            unsigned attribute_index = 512 + sprite * 4;
            u8 raw_y = vdp->vram[sprite2_attribute_address(
                vdp, attribute_index, planar)];
            unsigned top = ((unsigned)raw_y + 1) & 0xff;
            unsigned sprite_line = (display_y + 256 - top) & 0xff;
            unsigned pattern_line;
            u8 colour_attribute;

            if (sprite_line >= (unsigned)effective_size)
                continue;
            if (visible_count == 8) {
                if (ninth_sprite < 0)
                    ninth_sprite = (int)sprite;
                continue;
            }

            pattern_line = sprite_line / (unsigned)scale;
            colour_attribute =
                vdp->vram[sprite2_attribute_address(
                    vdp, sprite * 16 + pattern_line, planar)];
            visible[visible_count].x =
                vdp->vram[sprite2_attribute_address(
                    vdp, attribute_index + 1, planar)] -
                (colour_attribute & 0x80 ? 32 : 0);
            visible[visible_count].pattern = sprite_pattern_line(
                vdp,
                vdp->vram[sprite2_attribute_address(
                    vdp, attribute_index + 2, planar)],
                size, pattern_line, planar);
            visible[visible_count].attribute = colour_attribute;
            ++visible_count;
        }

        /*
         * CC and IC sprites do not participate in collision detection.
         * On V99x8, transparent color zero is collision-inactive too.
         */
        for (unsigned sprite = 0; sprite < visible_count; ++sprite) {
            const SpriteLine *line = &visible[sprite];
            u8 colour = line->attribute & 0x0f;

            if ((line->attribute & 0x60) ||
                (!colour && !(vdp->registers[8] & 0x20)))
                continue;
            for (int x = line->x;
                 x < line->x + effective_size; ++x) {
                if ((unsigned)x >= MSX1_VIDEO_W ||
                    !sprite_dot(line, size, scale, x))
                    continue;
                if (occupied[x])
                    latch_sprite_collision(
                        vdp, x, (int)y, &collision);
                occupied[x] = true;
            }
        }

        /*
         * Draw low priority first so lower-numbered entries overdraw higher
         * ones. A CC entry is not drawn alone; its colour bits are ORed into
         * the nearest preceding non-CC sprite wherever their dots overlap.
         */
        for (int sprite = (int)visible_count - 1;
             sprite >= 0; --sprite) {
            const SpriteLine *line = &visible[sprite];
            u8 base_colour = line->attribute & 0x0f;

            if ((line->attribute & 0x40) ||
                (!base_colour && !(vdp->registers[8] & 0x20)))
                continue;
            for (int x = line->x;
                 x < line->x + effective_size; ++x) {
                u8 colour;

                if ((unsigned)x >= MSX1_VIDEO_W ||
                    !sprite_dot(line, size, scale, x))
                    continue;
                colour = base_colour;
                for (unsigned combined = (unsigned)sprite + 1;
                     combined < visible_count &&
                     (visible[combined].attribute & 0x40);
                     ++combined) {
                    if (sprite_dot(
                            &visible[combined], size, scale, x)) {
                        colour |=
                            visible[combined].attribute & 0x0f;
                    }
                }
                draw_sprite2_pixel(vdp, mode, x, (int)y, colour);
            }
        }
    }

    update_sprite_status(vdp, ninth_sprite, last_sprite, collision);
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
    vdp->command_source_x = 0;
    vdp->command_source_y = 0;
    vdp->command_source_origin_x = 0;
    vdp->command_row_length = 0;
    vdp->command_remaining_x = 0;
    vdp->command_remaining_y = 0;
    vdp->command_border_x = 0;
    vdp->command_line_major = 0;
    vdp->command_line_minor = 0;
    vdp->command_line_error = 0;
    vdp->command_slot_phase = 0;
    vdp->sprite_collision_x = 0;
    vdp->sprite_collision_y = 0;
    vdp->command_code = 0;
    vdp->command_mode = 0;
    vdp->command_argument = 0;
    vdp->command_colour = 0;
    vdp->command_operation = 0;
    vdp->command_event = V9938_COMMAND_EVENT_NONE;
    vdp->command_transfer_pending = false;
    vdp->command_ticks_remaining = 0;
    vdp->command_tick_fraction = 0;
    vdp->timing_cycle = 0;
    vdp->timing_frame_cycles = 0;
    vdp->timing_scanlines = 0;
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
    if (vdp->type == MSX_VDP_V9938 &&
        reg == 0 && !(value & 0x10))
        vdp->status1 &= (u8)~0x01;
    if (reg == 0 || reg == 1)
        update_irq(vdp);
    if (reg == 16)
        vdp->palette_pending = false;
    if (vdp->type == MSX_VDP_V9938 && reg == 44) {
        if (command_has_clock(vdp) &&
            vdp->command_event == V9938_COMMAND_EVENT_NONE)
            vdp->command_slot_phase =
                command_current_slot_phase(vdp);
        command_transfer_write(vdp);
    }
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
            update_irq(vdp);
            break;
        case 1:
            value = vdp->status1 & (u8)~0x01;
            if (vdp->registers[0] & 0x10) {
                value |= vdp->status1 & 0x01;
            } else {
                u64 match_tick;

                /*
                 * With IE1 disabled FH is a non-latching pulse from the
                 * matching right border into the next line's left border.
                 */
                if (v9938_hscan_tick(vdp, &match_tick)) {
                    u64 frame_ticks =
                        (u64)vdp->timing_scanlines *
                        V9938_TICKS_PER_LINE;
                    u64 beam_ticks =
                        (u64)vdp->timing_cycle * frame_ticks /
                        vdp->timing_frame_cycles;
                    u64 after_match =
                        (beam_ticks + frame_ticks - match_tick) %
                        frame_ticks;
                    unsigned pulse_ticks =
                        (vdp->registers[1] & 0x10) ? 316 : 288;

                    if (after_match < pulse_ticks)
                        value |= 0x01;
                }
            }
            vdp->status1 &= (u8)~0x01;
            update_irq(vdp);
            break;
        case 2:
            value = vdp->status2;
            break;
        case 3:
            value = (u8)vdp->sprite_collision_x;
            break;
        case 4:
            value = (u8)((vdp->sprite_collision_x >> 8) | 0xfe);
            break;
        case 5:
            value = (u8)vdp->sprite_collision_y;
            vdp->sprite_collision_x = 0;
            vdp->sprite_collision_y = 0;
            break;
        case 6:
            value = (u8)((vdp->sprite_collision_y >> 8) | 0xfc);
            break;
        case 7:
            value = command_read_colour(vdp);
            break;
        case 8:
            value = (u8)vdp->command_border_x;
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

static void update_retrace_status(MsxVdp *vdp) {
    u64 frame_ticks;
    u64 beam_ticks;
    u64 display_start;
    u64 display_end;
    unsigned line_zero;
    unsigned line_phase;
    unsigned visible_lines;
    int right_border;
    unsigned blank_length;
    bool text_mode;

    if (vdp->type != MSX_VDP_V9938 ||
        !vdp->timing_frame_cycles || !vdp->timing_scanlines)
        return;
    frame_ticks =
        (u64)vdp->timing_scanlines * V9938_TICKS_PER_LINE;
    beam_ticks =
        (u64)vdp->timing_cycle * frame_ticks /
        vdp->timing_frame_cycles;
    line_phase = (unsigned)(
        beam_ticks % V9938_TICKS_PER_LINE);
    visible_lines = vdp->registers[9] & 0x80
                  ? MSX2_VIDEO_H : MSX1_VIDEO_H;
    line_zero = v9938_line_zero(vdp);
    display_start =
        (u64)line_zero * V9938_TICKS_PER_LINE +
        V9938_LEFT_BORDER_TICKS;
    display_end =
        display_start +
        (u64)visible_lines * V9938_TICKS_PER_LINE;

    vdp->status2 &= (u8)~0x60;
    if (beam_ticks < display_start - V9938_TICKS_PER_LINE ||
        beam_ticks >= display_end)
        vdp->status2 |= 0x40;

    text_mode = (vdp->registers[1] & 0x10) != 0;
    right_border = (int)v9938_right_border(vdp);
    blank_length = text_mode ? 404 : 312;
    if ((line_phase + V9938_TICKS_PER_LINE -
         (unsigned)right_border) %
        V9938_TICKS_PER_LINE < blank_length)
        vdp->status2 |= 0x20;
}

void vdp_begin_frame(MsxVdp *vdp, unsigned frame_cycles,
                     unsigned scanlines) {
    if (!vdp)
        return;
    vdp->timing_cycle = 0;
    vdp->timing_frame_cycles = frame_cycles;
    vdp->timing_scanlines = scanlines;
    update_retrace_status(vdp);
}

void vdp_advance(MsxVdp *vdp, unsigned cycles) {
    u64 match_tick;
    u64 frame_ticks;
    u64 match_cycle;
    u64 first_match;
    u64 end_cycle;

    if (!vdp || !vdp->timing_frame_cycles)
        return;
    if (vdp->type == MSX_VDP_V9938 &&
        vdp->command_event != V9938_COMMAND_EVENT_NONE) {
        u64 frame_ticks =
            (u64)vdp->timing_scanlines * V9938_TICKS_PER_LINE;
        u64 ticks_per_cycle =
            (frame_ticks << 32) / vdp->timing_frame_cycles;
        u64 elapsed =
            vdp->command_tick_fraction + (u64)cycles * ticks_per_cycle;

        vdp->command_tick_fraction = elapsed & 0xffffffffu;
        command_engine_advance(vdp, elapsed >> 32);
    }
    if (vdp->type == MSX_VDP_V9938 &&
        (vdp->registers[0] & 0x10) &&
        v9938_hscan_tick(vdp, &match_tick)) {
        frame_ticks =
            (u64)vdp->timing_scanlines * V9938_TICKS_PER_LINE;
        match_cycle =
            (match_tick * vdp->timing_frame_cycles +
             frame_ticks - 1) / frame_ticks;
        first_match = match_cycle;
        if (first_match <= vdp->timing_cycle)
            first_match += vdp->timing_frame_cycles;
        end_cycle = (u64)vdp->timing_cycle + cycles;
        if (first_match <= end_cycle) {
            vdp->status1 |= 0x01;
            update_irq(vdp);
        }
    }
    vdp->timing_cycle =
        (vdp->timing_cycle + cycles) % vdp->timing_frame_cycles;
    update_retrace_status(vdp);
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
    update_irq(vdp);
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
    vdp->command_event = V9938_COMMAND_EVENT_NONE;
    vdp->command_ticks_remaining = 0;
    vdp->command_tick_fraction = 0;
}

static bool command_has_clock(const MsxVdp *vdp) {
    return vdp->timing_frame_cycles && vdp->timing_scanlines;
}

static u16 command_current_slot_phase(const MsxVdp *vdp) {
    u64 frame_ticks =
        (u64)vdp->timing_scanlines * V9938_TICKS_PER_LINE;
    u64 beam_ticks =
        (u64)vdp->timing_cycle * frame_ticks /
        vdp->timing_frame_cycles;

    return (u16)(beam_ticks % V9938_TICKS_PER_LINE);
}

static u64 command_access_delay(const MsxVdp *vdp, u64 minimum) {
    const u16 *slots;
    size_t slot_count;
    u64 target = (u64)vdp->command_slot_phase + minimum;
    u64 lines = target / V9938_TICKS_PER_LINE;
    unsigned phase = (unsigned)(target % V9938_TICKS_PER_LINE);
    unsigned selected = 0;
    bool found = false;

    if (!(vdp->registers[1] & 0x40) || (vdp->status2 & 0x40)) {
        slots = v9938_slots_screen_off;
        slot_count = sizeof(v9938_slots_screen_off) /
                     sizeof(v9938_slots_screen_off[0]);
    } else if (vdp->registers[8] & 0x02) {
        slots = v9938_slots_sprites_off;
        slot_count = sizeof(v9938_slots_sprites_off) /
                     sizeof(v9938_slots_sprites_off[0]);
    } else {
        slots = v9938_slots_sprites_on;
        slot_count = sizeof(v9938_slots_sprites_on) /
                     sizeof(v9938_slots_sprites_on[0]);
    }

    for (size_t i = 0; i < slot_count; ++i) {
        if (slots[i] >= phase) {
            selected = slots[i];
            found = true;
            break;
        }
    }
    if (!found) {
        ++lines;
        selected = slots[0];
    }
    return lines * V9938_TICKS_PER_LINE +
           selected - vdp->command_slot_phase;
}

static void command_schedule_event(MsxVdp *vdp, u8 event, u64 ticks) {
    vdp->command_event = event;
    if (command_has_clock(vdp))
        ticks = command_access_delay(vdp, ticks);
    vdp->command_ticks_remaining = ticks ? ticks : 1;
    vdp->command_tick_fraction = 0;
}

static u64 command_transfer_ticks(const MsxVdp *vdp) {
    switch (vdp->command_code) {
        case 0x0a: /* LMCM: one VRAM read. */
            return 64;
        case 0x0b: /* LMMC: read-modify-write. */
            return 96;
        case 0x0f: /* HMMC: one packed-byte write. */
            return 48;
        default:
            return 1;
    }
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

static void command_engine_advance(MsxVdp *vdp, u64 ticks) {
    while (ticks &&
           vdp->command_event != V9938_COMMAND_EVENT_NONE) {
        u8 event;
        u64 event_ticks;

        if (ticks < vdp->command_ticks_remaining) {
            vdp->command_ticks_remaining -= ticks;
            vdp->command_slot_phase = (u16)(
                (vdp->command_slot_phase + ticks) %
                V9938_TICKS_PER_LINE);
            return;
        }
        event_ticks = vdp->command_ticks_remaining;
        ticks -= event_ticks;
        event = vdp->command_event;
        vdp->command_slot_phase = (u16)(
            (vdp->command_slot_phase + event_ticks) %
            V9938_TICKS_PER_LINE);
        vdp->command_event = V9938_COMMAND_EVENT_NONE;
        vdp->command_ticks_remaining = 0;
        vdp->command_tick_fraction = 0;

        if (event == V9938_COMMAND_EVENT_COMPLETE) {
            command_complete(vdp);
        } else if (event == V9938_COMMAND_EVENT_TRANSFER_READY &&
                   (vdp->status2 & V9938_STATUS2_CE)) {
            if (vdp->command_code == 0x0a)
                command_load_colour(vdp);
            vdp->status2 |= V9938_STATUS2_TR;
            if (vdp->command_transfer_pending &&
                (vdp->command_code == 0x0b ||
                 vdp->command_code == 0x0f))
                command_transfer_write(vdp);
        } else if (event == V9938_COMMAND_EVENT_STEP &&
                   (vdp->status2 & V9938_STATUS2_CE)) {
            command_execute_step(vdp);
        }
    }
}

static void command_start_steps(MsxVdp *vdp, u64 first_ticks) {
    command_schedule_event(
        vdp, V9938_COMMAND_EVENT_STEP, first_ticks);
    if (!command_has_clock(vdp))
        command_engine_advance(vdp, ~(u64)0);
}

static bool command_advance_block(MsxVdp *vdp,
                                  unsigned pixels_per_byte,
                                  bool *new_row) {
    int x_step = vdp->command_code >= 0x0c
               ? (int)pixels_per_byte : 1;
    int y_step = vdp->command_argument & 0x08 ? -1 : 1;
    bool move_source =
        vdp->command_code == 0x09 ||
        vdp->command_code == 0x0d ||
        vdp->command_code == 0x0e;

    *new_row = false;
    if (vdp->command_argument & 0x04)
        x_step = -x_step;
    if (--vdp->command_remaining_x) {
        vdp->command_x = (u16)(vdp->command_x + x_step);
        if (move_source)
            vdp->command_source_x =
                (u16)(vdp->command_source_x + x_step);
        return true;
    }
    if (!--vdp->command_remaining_y)
        return false;

    *new_row = true;
    vdp->command_x = vdp->command_origin_x;
    vdp->command_y = (u16)(vdp->command_y + y_step);
    if (move_source) {
        vdp->command_source_x = vdp->command_source_origin_x;
        vdp->command_source_y =
            (u16)(vdp->command_source_y + y_step);
    }
    vdp->command_remaining_x = vdp->command_row_length;
    return true;
}

static u64 command_block_ticks(u8 code, bool new_row) {
    switch (code) {
        case 0x08:
            return 96 + (new_row ? 64 : 0);
        case 0x09:
            return 120 + (new_row ? 64 : 0);
        case 0x0c:
            return 48 + (new_row ? 56 : 0);
        case 0x0d:
            return 88 + (new_row ? 64 : 0);
        case 0x0e:
            return 64;
        default:
            return 1;
    }
}

static void command_execute_line_step(MsxVdp *vdp, unsigned width) {
    int x_step = vdp->command_argument & 0x04 ? -1 : 1;
    int y_step = vdp->command_argument & 0x08 ? -1 : 1;
    bool minor_step = false;

    if (!(vdp->command_argument & 0x20))
        command_write_pixel(
            vdp, vdp->command_mode, vdp->command_x, vdp->command_y,
            vdp->command_colour, vdp->command_operation);
    if (!--vdp->command_remaining_x) {
        command_complete(vdp);
        return;
    }

    if (!(vdp->command_argument & 0x01)) {
        if (x_step < 0 && vdp->command_x == 0) {
            command_complete(vdp);
            return;
        }
        vdp->command_x = (u16)(vdp->command_x + x_step);
        if (vdp->command_line_error < vdp->command_line_minor) {
            vdp->command_line_error =
                (u16)(vdp->command_line_error +
                      vdp->command_line_major);
            if (y_step < 0 && vdp->command_y == 0) {
                command_complete(vdp);
                return;
            }
            vdp->command_y = (u16)(vdp->command_y + y_step);
            minor_step = true;
        }
    } else {
        if (y_step < 0 && vdp->command_y == 0) {
            command_complete(vdp);
            return;
        }
        vdp->command_y = (u16)(vdp->command_y + y_step);
        if (vdp->command_line_error < vdp->command_line_minor) {
            vdp->command_line_error =
                (u16)(vdp->command_line_error +
                      vdp->command_line_major);
            if (x_step < 0 && vdp->command_x == 0) {
                command_complete(vdp);
                return;
            }
            vdp->command_x = (u16)(vdp->command_x + x_step);
            minor_step = true;
        }
    }
    vdp->command_line_error =
        (vdp->command_line_error - vdp->command_line_minor) & 0x03ff;
    if (vdp->command_x >= width) {
        command_complete(vdp);
        return;
    }
    command_schedule_event(
        vdp, V9938_COMMAND_EVENT_STEP, minor_step ? 120 : 88);
}

static void command_execute_step(MsxVdp *vdp) {
    unsigned width = 0;
    unsigned colour_mask = 0;
    unsigned pixels_per_byte = 1;
    bool new_row;
    bool more;

    if (!command_mode_info(
            vdp->command_mode, &width, &colour_mask, &pixels_per_byte)) {
        command_complete(vdp);
        return;
    }
    (void)colour_mask;

    switch (vdp->command_code) {
        case 0x04: /* POINT */
            vdp->status7 = vdp->command_argument & 0x10
                         ? 0xff
                         : bitmap_pixel(
                               vdp, vdp->command_mode,
                               vdp->command_source_x,
                               vdp->command_source_y);
            vdp->registers[44] = vdp->status7;
            command_complete(vdp);
            break;

        case 0x05: /* PSET */
            if (!(vdp->command_argument & 0x20))
                command_write_pixel(
                    vdp, vdp->command_mode,
                    vdp->command_x, vdp->command_y,
                    vdp->command_colour, vdp->command_operation);
            command_complete(vdp);
            break;

        case 0x06: { /* SRCH */
            int x = vdp->command_x;
            u8 found = vdp->command_argument & 0x10
                     ? 0xff
                     : bitmap_pixel(
                           vdp, vdp->command_mode,
                           vdp->command_x, vdp->command_y);
            bool unequal = (vdp->command_argument & 0x02) != 0;
            int x_step = vdp->command_argument & 0x04 ? -1 : 1;

            if ((found == vdp->command_colour) != unequal) {
                vdp->status2 |= V9938_STATUS2_BD;
                vdp->command_border_x = vdp->command_x;
                command_complete(vdp);
                break;
            }
            x += x_step;
            if (vdp->command_remaining_y ||
                x < 0 || x >= (int)width) {
                vdp->command_border_x = (u16)x;
                command_complete(vdp);
                break;
            }
            vdp->command_x = (u16)x;
            command_schedule_event(
                vdp, V9938_COMMAND_EVENT_STEP, 88);
            break;
        }

        case 0x07: /* LINE */
            command_execute_line_step(vdp, width);
            break;

        case 0x08: /* LMMV */
            if (!(vdp->command_argument & 0x20))
                command_write_pixel(
                    vdp, vdp->command_mode,
                    vdp->command_x, vdp->command_y,
                    vdp->command_colour, vdp->command_operation);
            more = command_advance_block(
                vdp, pixels_per_byte, &new_row);
            if (!more)
                command_complete(vdp);
            else
                command_schedule_event(
                    vdp, V9938_COMMAND_EVENT_STEP,
                    command_block_ticks(vdp->command_code, new_row));
            break;

        case 0x09: { /* LMMM */
            u8 colour = vdp->command_argument & 0x10
                      ? 0xff
                      : bitmap_pixel(
                            vdp, vdp->command_mode,
                            vdp->command_source_x,
                            vdp->command_source_y);

            if (!(vdp->command_argument & 0x20))
                command_write_pixel(
                    vdp, vdp->command_mode,
                    vdp->command_x, vdp->command_y,
                    colour, vdp->command_operation);
            more = command_advance_block(
                vdp, pixels_per_byte, &new_row);
            if (!more)
                command_complete(vdp);
            else
                command_schedule_event(
                    vdp, V9938_COMMAND_EVENT_STEP,
                    command_block_ticks(vdp->command_code, new_row));
            break;
        }

        case 0x0c: { /* HMMV */
            if (!(vdp->command_argument & 0x20)) {
                unsigned address = wrap_address(
                    vdp, bitmap_address(
                        vdp->command_mode,
                        vdp->command_x, vdp->command_y));
                vdp->vram[address] = vdp->command_colour;
            }
            more = command_advance_block(
                vdp, pixels_per_byte, &new_row);
            if (!more)
                command_complete(vdp);
            else
                command_schedule_event(
                    vdp, V9938_COMMAND_EVENT_STEP,
                    command_block_ticks(vdp->command_code, new_row));
            break;
        }

        case 0x0d: { /* HMMM */
            u8 value = vdp->command_argument & 0x10
                     ? 0xff
                     : vdp->vram[wrap_address(
                           vdp, bitmap_address(
                               vdp->command_mode,
                               vdp->command_source_x,
                               vdp->command_source_y))];

            if (!(vdp->command_argument & 0x20))
                vdp->vram[wrap_address(
                    vdp, bitmap_address(
                        vdp->command_mode,
                        vdp->command_x, vdp->command_y))] = value;
            more = command_advance_block(
                vdp, pixels_per_byte, &new_row);
            if (!more)
                command_complete(vdp);
            else
                command_schedule_event(
                    vdp, V9938_COMMAND_EVENT_STEP,
                    command_block_ticks(vdp->command_code, new_row));
            break;
        }

        case 0x0e: { /* YMMM */
            unsigned source_address = wrap_address(
                vdp, bitmap_address(
                    vdp->command_mode,
                    vdp->command_source_x,
                    vdp->command_source_y));
            unsigned destination_address = wrap_address(
                vdp, bitmap_address(
                    vdp->command_mode,
                    vdp->command_x, vdp->command_y));

            if (!(vdp->command_argument & 0x20))
                vdp->vram[destination_address] =
                    vdp->vram[source_address];
            more = command_advance_block(
                vdp, pixels_per_byte, &new_row);
            if (!more)
                command_complete(vdp);
            else
                command_schedule_event(
                    vdp, V9938_COMMAND_EVENT_STEP,
                    command_block_ticks(vdp->command_code, new_row));
            break;
        }

        default:
            command_complete(vdp);
            break;
    }
}

static u8 command_read_colour(MsxVdp *vdp) {
    u8 value = vdp->status7;

    if (vdp->command_code == 0x0a &&
        (vdp->status2 & V9938_STATUS2_CE) &&
        (vdp->status2 & V9938_STATUS2_TR)) {
        unsigned width = 0;
        unsigned colour_mask = 0;
        unsigned pixels_per_byte = 1;
        bool more;

        command_mode_info(vdp->command_mode, &width, &colour_mask,
                          &pixels_per_byte);
        (void)width;
        (void)colour_mask;
        if (command_has_clock(vdp))
            vdp->command_slot_phase =
                command_current_slot_phase(vdp);
        more = command_advance_transfer(vdp, pixels_per_byte);
        if (!command_has_clock(vdp)) {
            if (more)
                command_load_colour(vdp);
            else
                command_complete(vdp);
        } else {
            vdp->status2 &= (u8)~V9938_STATUS2_TR;
            command_schedule_event(
                vdp, more
                   ? V9938_COMMAND_EVENT_TRANSFER_READY
                   : V9938_COMMAND_EVENT_COMPLETE,
                command_transfer_ticks(vdp));
        }
    } else if (!(vdp->status2 & V9938_STATUS2_CE)) {
        vdp->status2 &= (u8)~V9938_STATUS2_TR;
    }
    return value;
}

static void command_transfer_write(MsxVdp *vdp) {
    unsigned width;
    unsigned colour_mask;
    unsigned pixels_per_byte;

    vdp->command_transfer_pending = true;
    if (!(vdp->status2 & V9938_STATUS2_CE) ||
        !(vdp->status2 & V9938_STATUS2_TR) ||
        (vdp->command_code != 0x0b &&
         vdp->command_code != 0x0f) ||
        !command_mode_info(vdp->command_mode, &width, &colour_mask,
                           &pixels_per_byte))
        return;
    (void)width;
    (void)colour_mask;
    vdp->command_transfer_pending = false;
    if (command_has_clock(vdp))
        vdp->status2 &= (u8)~V9938_STATUS2_TR;
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
    if (command_advance_transfer(vdp, pixels_per_byte)) {
        if (command_has_clock(vdp)) {
            command_schedule_event(
                vdp, V9938_COMMAND_EVENT_TRANSFER_READY,
                command_transfer_ticks(vdp));
        } else {
            vdp->status2 |= V9938_STATUS2_TR;
        }
    } else if (command_has_clock(vdp)) {
        command_schedule_event(
            vdp, V9938_COMMAND_EVENT_COMPLETE,
            command_transfer_ticks(vdp));
    } else {
        command_complete(vdp);
    }
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
    unsigned width;
    unsigned colour_mask;
    unsigned pixels_per_byte;
    unsigned sx = command_coordinate(vdp, 32, 0x01);
    unsigned sy = command_coordinate(vdp, 34, 0x03);
    unsigned dx = command_coordinate(vdp, 36, 0x01);
    unsigned dy = command_coordinate(vdp, 38, 0x03);
    unsigned nx = command_coordinate(vdp, 40, 0x03);
    unsigned ny = command_coordinate(vdp, 42, 0x03);

    /*
     * A write to R#46 replaces any command already in progress. Its
     * scheduled CE/TR transition therefore belongs to the old command.
     */
    vdp->command_event = V9938_COMMAND_EVENT_NONE;
    vdp->command_ticks_remaining = 0;
    vdp->command_tick_fraction = 0;
    if (command_has_clock(vdp))
        vdp->command_slot_phase =
            command_current_slot_phase(vdp);
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
    vdp->command_colour = vdp->registers[44];
    vdp->command_operation = command & 0x0f;

    switch (code) {
        case 0x04: /* POINT */
            vdp->command_source_x = (u16)sx;
            vdp->command_source_y = (u16)sy;
            command_start_steps(vdp, 32);
            break;

        case 0x05: /* PSET */
            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            command_start_steps(vdp, 48);
            break;

        case 0x06: { /* SRCH */
            vdp->command_x = (u16)sx;
            vdp->command_y = (u16)sy;
            vdp->command_colour =
                vdp->registers[44] & (u8)colour_mask;
            vdp->command_remaining_y = sx >= width ? 1 : 0;
            command_start_steps(vdp, 88);
            break;
        }

        case 0x07: { /* LINE */
            unsigned major = nx & 0x03ff;
            unsigned minor = ny & 0x03ff;

            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            vdp->command_line_major = (u16)major;
            vdp->command_line_minor = (u16)minor;
            vdp->command_line_error =
                (u16)(major ? (major - 1) >> 1 : 0);
            vdp->command_remaining_x = (u16)(major + 1);
            command_start_steps(vdp, 88);
            break;
        }

        case 0x08: { /* LMMV */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, false);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);

            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            vdp->command_origin_x = (u16)dx;
            vdp->command_row_length = (u16)columns;
            vdp->command_remaining_x = (u16)columns;
            vdp->command_remaining_y = (u16)rows;
            command_start_steps(vdp, command_block_ticks(code, false));
            break;
        }

        case 0x09: { /* LMMM */
            unsigned columns = command_clip_x2(
                sx, dx, nx, width, reverse_x, pixels_per_byte, false);
            unsigned rows = command_clip_y2(sy, dy, ny, reverse_y);

            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            vdp->command_origin_x = (u16)dx;
            vdp->command_source_x = (u16)sx;
            vdp->command_source_y = (u16)sy;
            vdp->command_source_origin_x = (u16)sx;
            vdp->command_row_length = (u16)columns;
            vdp->command_remaining_x = (u16)columns;
            vdp->command_remaining_y = (u16)rows;
            command_start_steps(vdp, command_block_ticks(code, false));
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
            if (vdp->command_transfer_pending)
                command_transfer_write(vdp);
            break;
        }

        case 0x0c: { /* HMMV */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);

            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            vdp->command_origin_x = (u16)dx;
            vdp->command_row_length = (u16)columns;
            vdp->command_remaining_x = (u16)columns;
            vdp->command_remaining_y = (u16)rows;
            command_start_steps(vdp, command_block_ticks(code, false));
            break;
        }

        case 0x0d: { /* HMMM */
            unsigned columns = command_clip_x2(
                sx, dx, nx, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y2(sy, dy, ny, reverse_y);

            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            vdp->command_origin_x = (u16)dx;
            vdp->command_source_x = (u16)sx;
            vdp->command_source_y = (u16)sy;
            vdp->command_source_origin_x = (u16)sx;
            vdp->command_row_length = (u16)columns;
            vdp->command_remaining_x = (u16)columns;
            vdp->command_remaining_y = (u16)rows;
            command_start_steps(vdp, command_block_ticks(code, false));
            break;
        }

        case 0x0e: { /* YMMM */
            unsigned columns = command_clip_x1(
                dx, 512, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y2(sy, dy, ny, reverse_y);

            vdp->command_x = (u16)dx;
            vdp->command_y = (u16)dy;
            vdp->command_origin_x = (u16)dx;
            vdp->command_source_x = (u16)dx;
            vdp->command_source_y = (u16)sy;
            vdp->command_source_origin_x = (u16)dx;
            vdp->command_row_length = (u16)columns;
            vdp->command_remaining_x = (u16)columns;
            vdp->command_remaining_y = (u16)rows;
            command_start_steps(vdp, command_block_ticks(code, false));
            break;
        }

        case 0x0f: { /* HMMC: high-speed CPU-to-VRAM transfer. */
            unsigned columns = command_clip_x1(
                dx, nx, width, reverse_x, pixels_per_byte, true);
            unsigned rows = command_clip_y1(dy, ny, reverse_y);

            command_setup_transfer(
                vdp, code, mode, dx, dy, columns, rows);
            if (vdp->command_transfer_pending)
                command_transfer_write(vdp);
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
    int sprites;

    if (!vdp)
        return;
    mode = display_mode(vdp);
    sprites = 0;
    switch (mode) {
        case 0x00: /* Graphics 1 */
        case 0x02: /* Multicolour */
        case 0x04: /* Graphics 2 */
            sprites = 1;
            break;
        case 0x06: /* Undocumented Multicolour/Graphics 2 combination. */
            sprites = vdp->type == MSX_VDP_TMS9918 ? 1 : 0;
            break;
        case 0x08: /* SCREEN 4 */
        case 0x0c: /* SCREEN 5 */
        case 0x10: /* SCREEN 6 */
        case 0x14: /* SCREEN 7 */
        case 0x1c: /* SCREEN 8 */
            sprites = vdp->type == MSX_VDP_V9938 ? 2 : 0;
            break;
        default:
            break;
    }
    if (vdp->type == MSX_VDP_V9938 &&
        (mode == 0x08 || mode == 0x0c || mode == 0x10 ||
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
    } else if (vdp->registers[1] & 0x10) {
        render_text(vdp);
        return;
    } else if (mode == 0x04 || mode == 0x08)
        render_graphics_2(vdp);
    else if (mode == 0x02)
        render_multicolour(vdp);
    else
        render_graphics_1(vdp);

    if (vdp->type == MSX_VDP_V9938 &&
        (vdp->registers[8] & 0x02))
        return;
    if (sprites == 1)
        render_sprites_mode1(vdp);
    else if (sprites == 2)
        render_sprites_mode2(vdp, mode);
}
