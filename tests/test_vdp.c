#include "vdp.h"

#include <assert.h>

#define SPRITE_ATTRIBUTE_BASE 0x1b00u
#define SPRITE_PATTERN_BASE   0x3000u
#define SPRITE2_COLOUR_BASE   0x17800u
#define SPRITE2_ATTRIBUTE_BASE 0x17a00u
#define SPRITE2_PATTERN_BASE  0x0e000u

#define COLOUR_BACKDROP 0x000000u
#define COLOUR_GREEN    0x3eb849u
#define COLOUR_LT_GREEN 0x74d07du
#define COLOUR_BLUE     0x5955e0u
#define COLOUR_LT_BLUE  0x8076f1u
#define COLOUR_DK_RED   0xb95e51u
#define COLOUR_LT_RED   0xff897du
#define COLOUR_WHITE    0xffffffu
#define V9938_GREEN     0x24db24u
#define V9938_LT_GREEN  0x6dff6du
#define V9938_DK_RED    0xb62424u
#define SCREEN8_RED     0x6d0000u

static u32 pixel(const MsxVdp *vdp, int x, int y) {
    return vdp->pixels[y * vdp->render_width + x];
}

static void write_control_register(MsxVdp *vdp, unsigned reg, u8 value) {
    assert(reg < MSX_VDP_REGISTER_COUNT);
    vdp_write_control(vdp, value);
    vdp_write_control(vdp, (u8)(0x80 | reg));
}

static void set_vram_address(MsxVdp *vdp, u16 address, bool write) {
    assert(address < 0x4000);
    vdp_write_control(vdp, (u8)address);
    vdp_write_control(vdp,
                      (u8)((address >> 8) | (write ? 0x40 : 0x00)));
}

static void setup_vdp(MsxVdp *vdp) {
    vdp_init(vdp);
    vdp->registers[1] = 0x40; /* display on, 8x8, no magnification */
    vdp->registers[5] = SPRITE_ATTRIBUTE_BASE >> 7;
    vdp->registers[6] = SPRITE_PATTERN_BASE >> 11;
    vdp->registers[7] = 0x01;
    vdp->vram[SPRITE_ATTRIBUTE_BASE] = 0xd0;
}

static void set_sprite(MsxVdp *vdp, unsigned index, u8 y, u8 x,
                       u8 pattern, u8 colour_attribute) {
    unsigned offset = SPRITE_ATTRIBUTE_BASE + index * 4;

    assert(index < 32);
    vdp->vram[offset + 0] = y;
    vdp->vram[offset + 1] = x;
    vdp->vram[offset + 2] = pattern;
    vdp->vram[offset + 3] = colour_attribute;
}

static void terminate_sprites(MsxVdp *vdp, unsigned index) {
    assert(index < 32);
    vdp->vram[SPRITE_ATTRIBUTE_BASE + index * 4] = 0xd0;
}

static void set_pattern(MsxVdp *vdp, unsigned pattern, unsigned row,
                        u8 bits) {
    assert(pattern < 256);
    assert(row < 8);
    vdp->vram[SPRITE_PATTERN_BASE + pattern * 8 + row] = bits;
}

static unsigned planar_address(unsigned address) {
    return ((address & 1) << 16) |
           ((address & 0x1fffe) >> 1);
}

static bool sprite2_planar(u8 mode) {
    return mode == 0x14 || mode == 0x1c;
}

static unsigned sprite2_address(u8 mode, unsigned address) {
    return sprite2_planar(mode) ? planar_address(address) : address;
}

static void setup_v9938_sprite2(MsxVdp *vdp, u8 mode) {
    assert(mode == 0x08 || mode == 0x0c || mode == 0x10 ||
           mode == 0x14 || mode == 0x1c);
    vdp_init(vdp);
    vdp_set_type(vdp, MSX_VDP_V9938);
    vdp_reset(vdp);
    vdp->registers[0] = mode >> 1;
    vdp->registers[1] = 0x40;
    vdp->registers[5] = 0xf7;
    vdp->registers[6] = SPRITE2_PATTERN_BASE >> 11;
    vdp->registers[7] = 0x01;
    vdp->registers[11] = 0x02;
    vdp->vram[sprite2_address(mode, SPRITE2_ATTRIBUTE_BASE)] = 0xd8;
}

static void set_sprite2(MsxVdp *vdp, u8 mode, unsigned index,
                        u8 y, u8 x, u8 pattern) {
    unsigned offset = SPRITE2_ATTRIBUTE_BASE + index * 4;

    assert(index < 32);
    vdp->vram[sprite2_address(mode, offset + 0)] = y;
    vdp->vram[sprite2_address(mode, offset + 1)] = x;
    vdp->vram[sprite2_address(mode, offset + 2)] = pattern;
}

static void set_sprite2_colour(MsxVdp *vdp, u8 mode, unsigned index,
                               unsigned line, u8 attribute) {
    unsigned offset = SPRITE2_COLOUR_BASE + index * 16 + line;

    assert(index < 32);
    assert(line < 16);
    vdp->vram[sprite2_address(mode, offset)] = attribute;
}

static void terminate_sprites2(MsxVdp *vdp, u8 mode, unsigned index) {
    unsigned offset = SPRITE2_ATTRIBUTE_BASE + index * 4;

    assert(index < 32);
    vdp->vram[sprite2_address(mode, offset)] = 0xd8;
}

static void set_pattern2(MsxVdp *vdp, u8 mode, unsigned pattern,
                         unsigned row, u8 bits) {
    unsigned offset = SPRITE2_PATTERN_BASE + pattern * 8 + row;

    assert(pattern < 256);
    assert(row < 8);
    vdp->vram[sprite2_address(mode, offset)] = bits;
}

static void test_basic_position_wrap_and_terminator(void) {
    MsxVdp vdp;

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x81);
    set_sprite(&vdp, 0, 9, 20, 0, 6);
    terminate_sprites(&vdp, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) == 0xb95e51u);
    assert(pixel(&vdp, 27, 10) == 0xb95e51u);
    assert(pixel(&vdp, 21, 10) == COLOUR_BACKDROP);
    assert(pixel(&vdp, 20, 9) == COLOUR_BACKDROP);

    /* Y=255 wraps to the first visible scanline. */
    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 255, 12, 0, 2);
    terminate_sprites(&vdp, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 12, 0) == COLOUR_GREEN);

    /* Y=0xd0 ends the list; later attributes are not evaluated. */
    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 0xd0, 0, 0, 0);
    set_sprite(&vdp, 1, 9, 30, 0, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 30, 10) == COLOUR_BACKDROP);
    assert(!(vdp.status & 0x60));
}

static void test_priority_transparency_and_collision(void) {
    MsxVdp vdp;
    u8 status;

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_pattern(&vdp, 1, 0, 0x80);
    set_sprite(&vdp, 0, 9, 40, 0, 2);
    set_sprite(&vdp, 1, 9, 40, 1, 3);
    terminate_sprites(&vdp, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 40, 10) == COLOUR_GREEN);
    assert(vdp.status & 0x20);

    /* Colour zero does not draw or block lower-priority colour, but on an
     * MSX1 VDP its pattern dots still participate in collision detection. */
    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_pattern(&vdp, 1, 0, 0x80);
    set_sprite(&vdp, 0, 9, 40, 0, 0);
    set_sprite(&vdp, 1, 9, 40, 1, 3);
    terminate_sprites(&vdp, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 40, 10) == COLOUR_LT_GREEN);
    assert(vdp.status & 0x20);
    status = vdp_read_status(&vdp);
    assert(status & 0x20);
    assert(!(vdp.status & 0x20));
    assert((vdp.status & 0x1f) == 2);

    /* Pattern overlap in the left border does not produce collision. */
    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0xff);
    set_pattern(&vdp, 1, 0, 0xff);
    set_sprite(&vdp, 0, 9, 0, 0, 0x82);
    set_sprite(&vdp, 1, 9, 0, 1, 0x83);
    terminate_sprites(&vdp, 2);
    vdp_render(&vdp);
    assert(!(vdp.status & 0x20));
}

static void test_early_clock_and_clipping(void) {
    MsxVdp vdp;

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0xff);
    set_sprite(&vdp, 0, 9, 28, 0, 0x82);
    terminate_sprites(&vdp, 1);
    vdp_render(&vdp);
    for (int x = 0; x < 4; ++x)
        assert(pixel(&vdp, x, 10) == COLOUR_GREEN);
    assert(pixel(&vdp, 4, 10) == COLOUR_BACKDROP);
    assert(pixel(&vdp, 28, 10) == COLOUR_BACKDROP);
}

static void test_size_and_magnification(void) {
    MsxVdp vdp;

    setup_vdp(&vdp);
    vdp.registers[1] = 0x42; /* display on, 16x16 */
    set_pattern(&vdp, 0, 0, 0x80); /* upper left */
    set_pattern(&vdp, 2, 0, 0x01); /* upper right */
    set_pattern(&vdp, 1, 0, 0x80); /* lower left */
    set_pattern(&vdp, 3, 0, 0x01); /* lower right */
    set_sprite(&vdp, 0, 19, 20, 3, 4); /* low pattern bits are ignored */
    terminate_sprites(&vdp, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 20) == COLOUR_BLUE);
    assert(pixel(&vdp, 35, 20) == COLOUR_BLUE);
    assert(pixel(&vdp, 20, 28) == COLOUR_BLUE);
    assert(pixel(&vdp, 35, 28) == COLOUR_BLUE);
    assert(pixel(&vdp, 21, 20) == COLOUR_BACKDROP);

    setup_vdp(&vdp);
    vdp.registers[1] = 0x41; /* display on, magnified 8x8 */
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 19, 50, 0, 5);
    terminate_sprites(&vdp, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 50, 20) == COLOUR_LT_BLUE);
    assert(pixel(&vdp, 51, 20) == COLOUR_LT_BLUE);
    assert(pixel(&vdp, 50, 21) == COLOUR_LT_BLUE);
    assert(pixel(&vdp, 51, 21) == COLOUR_LT_BLUE);
    assert(pixel(&vdp, 52, 20) == COLOUR_BACKDROP);
    assert(pixel(&vdp, 50, 22) == COLOUR_BACKDROP);

    setup_vdp(&vdp);
    vdp.registers[1] = 0x43; /* display on, magnified 16x16 */
    set_pattern(&vdp, 2, 0, 0x01);
    set_pattern(&vdp, 1, 0, 0x80);
    set_sprite(&vdp, 0, 19, 60, 3, 2);
    terminate_sprites(&vdp, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 90, 20) == COLOUR_GREEN);
    assert(pixel(&vdp, 91, 21) == COLOUR_GREEN);
    assert(pixel(&vdp, 60, 36) == COLOUR_GREEN);
    assert(pixel(&vdp, 61, 37) == COLOUR_GREEN);
}

static void test_four_sprite_limit_and_fifth_index(void) {
    MsxVdp vdp;
    u8 status;

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0xff);
    for (unsigned i = 0; i < 5; ++i)
        set_sprite(&vdp, i, 9, (u8)(i * 16), 0, (u8)(i + 2));
    terminate_sprites(&vdp, 5);
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 10) == COLOUR_GREEN);
    assert(pixel(&vdp, 16, 10) == COLOUR_LT_GREEN);
    assert(pixel(&vdp, 32, 10) == COLOUR_BLUE);
    assert(pixel(&vdp, 48, 10) == COLOUR_LT_BLUE);
    assert(pixel(&vdp, 64, 10) == COLOUR_BACKDROP);
    assert((vdp.status & 0x5f) == 0x44);
    status = vdp_read_status(&vdp);
    assert((status & 0x5f) == 0x44);
    assert((vdp.status & 0x7f) == 4);
    vdp_render(&vdp);
    assert((vdp.status & 0x5f) == 0x44);

    /* A fifth sprite is neither rendered nor collision-checked. */
    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    for (unsigned i = 0; i < 4; ++i)
        set_sprite(&vdp, i, 9, (u8)(i * 16), 0, 2);
    set_sprite(&vdp, 4, 9, 0, 0, 3);
    terminate_sprites(&vdp, 5);
    vdp_render(&vdp);
    assert((vdp.status & 0x5f) == 0x44);
    assert(!(vdp.status & 0x20));

    /* Five sprites distributed across different lines do not overflow. */
    setup_vdp(&vdp);
    for (unsigned i = 0; i < 4; ++i)
        set_sprite(&vdp, i, 9, (u8)(i * 16), 0, 2);
    set_sprite(&vdp, 4, 17, 64, 0, 2);
    terminate_sprites(&vdp, 5);
    vdp_render(&vdp);
    assert(!(vdp.status & 0x40));
}

static void test_first_overflow_is_scanline_ordered(void) {
    MsxVdp vdp;

    setup_vdp(&vdp);
    for (unsigned i = 0; i < 5; ++i)
        set_sprite(&vdp, i, 9, (u8)(i * 20), 0, 2);
    for (unsigned i = 5; i < 10; ++i)
        set_sprite(&vdp, i, 0, (u8)((i - 5) * 20), 0, 2);
    terminate_sprites(&vdp, 10);
    vdp_render(&vdp);

    /* Line 1 overflows first at sprite 9; line 10 later overflows at 4. */
    assert(vdp.status & 0x40);
    assert((vdp.status & 0x1f) == 9);
}

static void test_status_latching_and_vblank(void) {
    MsxVdp vdp;
    u8 status;

    setup_vdp(&vdp);
    vdp.registers[1] = 0x60; /* display and vertical interrupt enabled */
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 9, 0, 0, 2);
    set_sprite(&vdp, 1, 9, 0, 0, 3);
    set_sprite(&vdp, 2, 9, 16, 0, 4);
    set_sprite(&vdp, 3, 9, 32, 0, 5);
    set_sprite(&vdp, 4, 9, 48, 0, 6);
    terminate_sprites(&vdp, 5);
    vdp_end_frame(&vdp);
    assert(vdp.status == 0xe4);
    assert(vdp.irq);
    status = vdp_read_status(&vdp);
    assert(status == 0xe4);
    assert(vdp.status == 0x04);
    assert(!vdp.irq);

    /* With F already latched, a new fifth-sprite condition cannot replace
     * the retained sprite index until status is read. */
    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    for (unsigned i = 0; i < 5; ++i)
        set_sprite(&vdp, i, 9, (u8)(i * 16), 0, 2);
    terminate_sprites(&vdp, 5);
    vdp.status = 0x83;
    vdp_render(&vdp);
    assert(vdp.status == 0x83);
}

static void test_sprite_modes_and_display_gating(void) {
    MsxVdp vdp;

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 9, 20, 0, 2);
    terminate_sprites(&vdp, 1);
    vdp.registers[0] = 0x02; /* Graphics II */
    vdp.registers[1] = 0x40;
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) == COLOUR_GREEN);

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 9, 20, 0, 2);
    terminate_sprites(&vdp, 1);
    vdp.registers[1] = 0x48; /* Multicolour */
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) == COLOUR_GREEN);

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 9, 20, 0, 2);
    terminate_sprites(&vdp, 1);
    vdp.registers[1] = 0x50; /* Text mode has no sprites. */
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) != COLOUR_GREEN);
    assert(!(vdp.status & 0x60));

    setup_vdp(&vdp);
    set_pattern(&vdp, 0, 0, 0x80);
    set_sprite(&vdp, 0, 9, 20, 0, 2);
    terminate_sprites(&vdp, 1);
    vdp.registers[1] = 0x00; /* Blanked display has no sprite evaluation. */
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) == COLOUR_BACKDROP);
    assert(!(vdp.status & 0x60));
}

static void test_v9938_sprite_mode2_attributes_and_limits(void) {
    const u8 mode = 0x0c; /* SCREEN 5 */
    MsxVdp vdp;

    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x81);
    set_pattern2(&vdp, mode, 0, 1, 0x80);
    set_sprite2(&vdp, mode, 0, 9, 20, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 2);
    set_sprite2_colour(&vdp, mode, 0, 1, 3);
    terminate_sprites2(&vdp, mode, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) == V9938_GREEN);
    assert(pixel(&vdp, 27, 10) == V9938_GREEN);
    assert(pixel(&vdp, 20, 11) == V9938_LT_GREEN);

    /* R#23 scrolls sprite coordinates along with the display. */
    vdp.registers[23] = 1;
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 9) == V9938_GREEN);
    assert(pixel(&vdp, 20, 10) == V9938_LT_GREEN);

    /* EC is a per-line attribute in mode 2. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0xff);
    set_sprite2(&vdp, mode, 0, 9, 28, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 0x82);
    terminate_sprites2(&vdp, mode, 1);
    vdp_render(&vdp);
    for (int x = 0; x < 4; ++x)
        assert(pixel(&vdp, x, 10) == V9938_GREEN);
    assert(pixel(&vdp, 4, 10) == COLOUR_BACKDROP);

    /* Y=0xd8 ends a mode-2 list. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 0xd8, 0, 0);
    set_sprite2(&vdp, mode, 1, 9, 30, 0);
    set_sprite2_colour(&vdp, mode, 1, 0, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 30, 10) == COLOUR_BACKDROP);

    /* The first eight sprites draw; S#0 identifies the ninth. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    for (unsigned i = 0; i < 9; ++i) {
        set_sprite2(&vdp, mode, i, 9, (u8)(i * 16), 0);
        set_sprite2_colour(&vdp, mode, i, 0, 2);
    }
    terminate_sprites2(&vdp, mode, 9);
    vdp_render(&vdp);
    assert(pixel(&vdp, 112, 10) == V9938_GREEN);
    assert(pixel(&vdp, 128, 10) == COLOUR_BACKDROP);
    assert((vdp.status & 0x5f) == 0x48);

    /* SPD disables both drawing and sprite evaluation. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 9, 20, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 2);
    terminate_sprites2(&vdp, mode, 1);
    vdp.registers[8] = 0x02;
    vdp_render(&vdp);
    assert(pixel(&vdp, 20, 10) == COLOUR_BACKDROP);
    assert(!(vdp.status & 0x60));

    /* 16x16 magnification uses all 16 per-line color entries. */
    setup_v9938_sprite2(&vdp, mode);
    vdp.registers[1] = 0x43;
    set_pattern2(&vdp, mode, 2, 0, 0x01);
    set_pattern2(&vdp, mode, 1, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 19, 60, 3);
    set_sprite2_colour(&vdp, mode, 0, 0, 2);
    set_sprite2_colour(&vdp, mode, 0, 8, 3);
    terminate_sprites2(&vdp, mode, 1);
    vdp_render(&vdp);
    assert(pixel(&vdp, 90, 20) == V9938_GREEN);
    assert(pixel(&vdp, 91, 21) == V9938_GREEN);
    assert(pixel(&vdp, 60, 36) == V9938_LT_GREEN);
    assert(pixel(&vdp, 61, 37) == V9938_LT_GREEN);
}

static void test_v9938_sprite_mode2_combining_and_collision(void) {
    const u8 mode = 0x0c; /* SCREEN 5 */
    MsxVdp vdp;

    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 9, 40, 0);
    set_sprite2(&vdp, mode, 1, 9, 40, 0);
    set_sprite2(&vdp, mode, 2, 9, 40, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 2);
    set_sprite2_colour(&vdp, mode, 1, 0, 0x44);
    set_sprite2_colour(&vdp, mode, 2, 0, 3);
    terminate_sprites2(&vdp, mode, 3);
    vdp_render(&vdp);
    assert(pixel(&vdp, 40, 10) == V9938_DK_RED);
    assert(vdp.status & 0x20);

    /* The first collision is reported with the hardware X/Y offsets. */
    write_control_register(&vdp, 15, 3);
    assert(vdp_read_status(&vdp) == 52);
    write_control_register(&vdp, 15, 4);
    assert(vdp_read_status(&vdp) == 0xfe);
    write_control_register(&vdp, 15, 6);
    assert(vdp_read_status(&vdp) == 0xfc);
    write_control_register(&vdp, 15, 5);
    assert(vdp_read_status(&vdp) == 18);
    assert(vdp.status & 0x20);
    write_control_register(&vdp, 15, 3);
    assert(vdp_read_status(&vdp) == 0);

    /* IC prevents collision without affecting display priority. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 9, 40, 0);
    set_sprite2(&vdp, mode, 1, 9, 40, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 0x22);
    set_sprite2_colour(&vdp, mode, 1, 0, 3);
    terminate_sprites2(&vdp, mode, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 40, 10) == V9938_GREEN);
    assert(!(vdp.status & 0x20));

    /* A leading CC sprite has no base sprite into which it can combine. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 9, 50, 0);
    set_sprite2(&vdp, mode, 1, 9, 50, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 0x44);
    set_sprite2_colour(&vdp, mode, 1, 0, 2);
    terminate_sprites2(&vdp, mode, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 50, 10) == V9938_GREEN);
    assert(!(vdp.status & 0x20));

    /* Transparent color zero neither draws nor collides. */
    setup_v9938_sprite2(&vdp, mode);
    set_pattern2(&vdp, mode, 0, 0, 0x80);
    set_sprite2(&vdp, mode, 0, 9, 60, 0);
    set_sprite2(&vdp, mode, 1, 9, 60, 0);
    set_sprite2_colour(&vdp, mode, 0, 0, 0);
    set_sprite2_colour(&vdp, mode, 1, 0, 3);
    terminate_sprites2(&vdp, mode, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 60, 10) == V9938_LT_GREEN);
    assert(!(vdp.status & 0x20));

    /* TP makes color zero opaque and collision-active. */
    vdp.registers[8] = 0x20;
    vdp_render(&vdp);
    assert(pixel(&vdp, 60, 10) == COLOUR_BACKDROP);
    assert(vdp.status & 0x20);
}

static void test_v9938_sprite_mode2_display_formats(void) {
    MsxVdp vdp;

    /* SCREEN 4 is the character mode that selects sprite mode 2. */
    setup_v9938_sprite2(&vdp, 0x08);
    set_pattern2(&vdp, 0x08, 0, 0, 0x80);
    set_sprite2(&vdp, 0x08, 0, 9, 10, 0);
    set_sprite2_colour(&vdp, 0x08, 0, 0, 2);
    terminate_sprites2(&vdp, 0x08, 1);
    vdp_render(&vdp);
    assert(vdp.render_width == 256);
    assert(pixel(&vdp, 10, 10) == V9938_GREEN);

    /*
     * SCREEN 6 uses the high and low color pairs for the two physical
     * pixels represented by one sprite dot.
     */
    setup_v9938_sprite2(&vdp, 0x10);
    set_pattern2(&vdp, 0x10, 0, 0, 0x80);
    set_sprite2(&vdp, 0x10, 0, 9, 10, 0);
    set_sprite2_colour(&vdp, 0x10, 0, 0, 0x0e);
    terminate_sprites2(&vdp, 0x10, 1);
    vdp_render(&vdp);
    assert(vdp.render_width == 512);
    assert(pixel(&vdp, 20, 10) == V9938_LT_GREEN);
    assert(pixel(&vdp, 21, 10) == V9938_GREEN);

    /* SCREEN 7 fetches sprite data through planar VRAM addressing. */
    setup_v9938_sprite2(&vdp, 0x14);
    set_pattern2(&vdp, 0x14, 0, 0, 0x80);
    set_sprite2(&vdp, 0x14, 0, 9, 10, 0);
    set_sprite2_colour(&vdp, 0x14, 0, 0, 2);
    terminate_sprites2(&vdp, 0x14, 1);
    vdp_render(&vdp);
    assert(vdp.render_width == 512);
    assert(pixel(&vdp, 20, 10) == V9938_GREEN);
    assert(pixel(&vdp, 21, 10) == V9938_GREEN);

    /* SCREEN 8 has a fixed sprite palette independent of P#0-P#15. */
    setup_v9938_sprite2(&vdp, 0x1c);
    vdp.palette_grb[2] = 0x777;
    set_pattern2(&vdp, 0x1c, 0, 0, 0x80);
    set_sprite2(&vdp, 0x1c, 0, 9, 10, 0);
    set_sprite2_colour(&vdp, 0x1c, 0, 0, 2);
    terminate_sprites2(&vdp, 0x1c, 1);
    vdp_render(&vdp);
    assert(vdp.render_width == 256);
    assert(pixel(&vdp, 10, 10) == SCREEN8_RED);
}

static void test_graphics_2_and_multicolour_mode_bits(void) {
    MsxVdp vdp;

    /*
     * Standard Graphics II layout: name table at 0x1800, pattern table at
     * 0x0000, and colour table at 0x2000. M3 is R0 bit 1.
     */
    vdp_init(&vdp);
    vdp.registers[0] = 0x02;
    vdp.registers[1] = 0x40;
    vdp.registers[2] = 0x06;
    vdp.registers[3] = 0xff;
    vdp.registers[4] = 0x03;
    vdp.registers[7] = 0x01;
    vdp.vram[0x1800] = 0;
    vdp.vram[0x0000] = 0x80;
    vdp.vram[0x2000] = 0xf1;
    vdp.vram[SPRITE_ATTRIBUTE_BASE] = 0xd0;
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 0) == COLOUR_WHITE);
    assert(pixel(&vdp, 1, 0) == COLOUR_BACKDROP);

    /*
     * In Multicolour, M2 is R1 bit 3 and each pattern byte supplies two
     * four-pixel colour blocks.
     */
    vdp_init(&vdp);
    vdp.registers[1] = 0x48;
    vdp.registers[2] = 0x06;
    vdp.registers[4] = 0x00;
    vdp.registers[7] = 0x01;
    vdp.vram[0x1800] = 0;
    vdp.vram[0x0000] = 0xf6;
    vdp.vram[SPRITE_ATTRIBUTE_BASE] = 0xd0;
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 0) == COLOUR_WHITE);
    assert(pixel(&vdp, 3, 0) == COLOUR_WHITE);
    assert(pixel(&vdp, 4, 0) == COLOUR_DK_RED);
    assert(pixel(&vdp, 7, 0) == COLOUR_DK_RED);
}

static void test_backdrop_and_text_background_colours(void) {
    MsxVdp vdp;

    /* A blanked display is entirely the R7 backdrop colour. */
    vdp_init(&vdp);
    vdp.registers[7] = 0x04;
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 0) == COLOUR_BLUE);
    assert(pixel(&vdp, 255, 191) == COLOUR_BLUE);

    /* In Text mode the low nibble of R7 supplies the background colour. */
    vdp.registers[1] = 0x50;
    vdp.registers[7] = 0x1f;
    vdp_render(&vdp);
    assert(pixel(&vdp, 8, 0) == COLOUR_WHITE);
    vdp.registers[7] = 0x19;
    vdp_render(&vdp);
    assert(pixel(&vdp, 8, 0) == COLOUR_LT_RED);
}

static void test_v9938_registers_and_status_selection(void) {
    MsxVdp vdp;

    vdp_init(&vdp);
    assert(vdp.type == MSX_VDP_TMS9918);
    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);
    assert(vdp.type == MSX_VDP_V9938);
    assert(vdp.registers[21] == 0x3b);
    assert(vdp.registers[22] == 0x05);
    assert(vdp.status2 == 0x0c);

    write_control_register(&vdp, 8, 0xff);
    assert(vdp.registers[8] == 0xfb);
    assert(vdp.registers[0] == 0);
    write_control_register(&vdp, 14, 0xff);
    assert(vdp.registers[14] == 0x07);
    write_control_register(&vdp, 17, 0xff);
    assert(vdp.registers[17] == 0xbf);
    write_control_register(&vdp, 24, 0xff);
    assert(vdp.registers[24] == 0);
    write_control_register(&vdp, 32, 0x5a);
    assert(vdp.registers[32] == 0x5a);
    write_control_register(&vdp, 47, 0x77);
    assert(vdp.registers[47] == 0);

    /* On a V9938, 11xxxxxx is not a register-write command. */
    vdp_write_control(&vdp, 0x12);
    vdp_write_control(&vdp, 0xc8);
    assert(vdp.registers[8] == 0xfb);

    vdp.status = 0xe4;
    vdp.irq = true;
    write_control_register(&vdp, 15, 2);
    assert(vdp_read_status(&vdp) == 0x0c);
    assert(vdp.status == 0xe4);
    assert(vdp.irq);
    write_control_register(&vdp, 15, 0);
    assert(vdp_read_status(&vdp) == 0xe4);
    assert(vdp.status == 0x04);
    assert(!vdp.irq);
    write_control_register(&vdp, 15, 15);
    assert(vdp_read_status(&vdp) == 0xff);
}

static void test_v9938_retrace_status(void) {
    enum {
        TICKS_PER_LINE = 1368,
        PAL_LINES = 313,
        PAL_FRAME_TICKS = TICKS_PER_LINE * PAL_LINES,
    };
    MsxVdp vdp;
    u8 status;
    unsigned display_start;
    unsigned vr_low;
    unsigned vr_high;

    vdp_init(&vdp);
    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);
    write_control_register(&vdp, 15, 2);

    /*
     * VR is high in the borders, falls at the start of the left border
     * preceding line zero, then rises after the visible 192 lines.
     */
    display_start = (16 + 36 + 10 + 7) * TICKS_PER_LINE + 202;
    vr_low = display_start - TICKS_PER_LINE;
    vr_high = display_start + MSX1_VIDEO_H * TICKS_PER_LINE;
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    status = vdp_read_status(&vdp);
    assert(status & 0x40);
    vdp_advance(&vdp, vr_low - 1);
    assert(vdp_read_status(&vdp) & 0x40);
    vdp_advance(&vdp, 1);
    assert(!(vdp_read_status(&vdp) & 0x40));
    vdp_advance(&vdp, vr_high - vr_low - 1);
    assert(!(vdp_read_status(&vdp) & 0x40));
    vdp_advance(&vdp, 1);
    assert(vdp_read_status(&vdp) & 0x40);

    /* R9 bit 7 starts ten lines earlier and selects 212 visible lines. */
    write_control_register(&vdp, 9, 0x80);
    display_start = (16 + 36 + 7) * TICKS_PER_LINE + 202;
    vr_low = display_start - TICKS_PER_LINE;
    vr_high = display_start + MSX2_VIDEO_H * TICKS_PER_LINE;
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, vr_low);
    assert(!(vdp_read_status(&vdp) & 0x40));
    vdp_advance(&vdp, vr_high - vr_low);
    assert(vdp_read_status(&vdp) & 0x40);

    /* R18 moves the vertical window; NTSC has the shorter top border. */
    write_control_register(&vdp, 9, 0);
    write_control_register(&vdp, 18, 0x70);
    display_start = (16 + 36 + 10) * TICKS_PER_LINE + 202;
    vr_low = display_start - TICKS_PER_LINE;
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, vr_low);
    assert(!(vdp_read_status(&vdp) & 0x40));

    write_control_register(&vdp, 18, 0);
    display_start = (16 + 9 + 10 + 7) * TICKS_PER_LINE + 202;
    vr_low = display_start - TICKS_PER_LINE;
    vdp_begin_frame(&vdp, TICKS_PER_LINE * 262, 262);
    vdp_advance(&vdp, vr_low);
    assert(!(vdp_read_status(&vdp) & 0x40));

    /* In a graphics mode HR covers ticks 1282..225 of every line. */
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    assert(vdp_read_status(&vdp) & 0x20);
    vdp_advance(&vdp, 225);
    assert(vdp_read_status(&vdp) & 0x20);
    vdp_advance(&vdp, 1);
    assert(!(vdp_read_status(&vdp) & 0x20));
    vdp_advance(&vdp, 1282 - 226);
    assert(vdp_read_status(&vdp) & 0x20);

    /* R18 horizontal adjustment zero shifts blanking 28 ticks left. */
    write_control_register(&vdp, 18, 0x07);
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, 197);
    assert(vdp_read_status(&vdp) & 0x20);
    vdp_advance(&vdp, 1);
    assert(!(vdp_read_status(&vdp) & 0x20));
    vdp_advance(&vdp, 1254 - 198);
    assert(vdp_read_status(&vdp) & 0x20);

    /* Text has a wider blanking interval, from tick 1254 through 289. */
    write_control_register(&vdp, 18, 0);
    write_control_register(&vdp, 1, 0x10);
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, 289);
    assert(vdp_read_status(&vdp) & 0x20);
    vdp_advance(&vdp, 1);
    assert(!(vdp_read_status(&vdp) & 0x20));
    vdp_advance(&vdp, 1254 - 290);
    assert(vdp_read_status(&vdp) & 0x20);
}

static void test_v9938_line_interrupt(void) {
    enum {
        TICKS_PER_LINE = 1368,
        PAL_LINES = 313,
        PAL_FRAME_TICKS = TICKS_PER_LINE * PAL_LINES,
        DEFAULT_LINE_ZERO = 16 + 36 + 10 + 7,
        DEFAULT_RIGHT_BORDER = 1282,
    };
    MsxVdp vdp;
    unsigned match_tick;

    vdp_init(&vdp);
    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);
    write_control_register(&vdp, 15, 1);
    write_control_register(&vdp, 19, 3);
    write_control_register(&vdp, 0, 0x10); /* IE1 */
    match_tick =
        (DEFAULT_LINE_ZERO + 3) * TICKS_PER_LINE +
        DEFAULT_RIGHT_BORDER;

    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, match_tick - 1);
    assert(!(vdp.status1 & 0x01));
    assert(!vdp.irq);
    assert(!(vdp_read_status(&vdp) & 0x01));
    vdp_advance(&vdp, 1);
    assert(vdp.status1 & 0x01);
    assert(vdp.irq);

    /* Reading S#1 acknowledges FH and releases its interrupt source. */
    assert(vdp_read_status(&vdp) & 0x01);
    assert(!(vdp.status1 & 0x01));
    assert(!vdp.irq);
    assert(!(vdp_read_status(&vdp) & 0x01));

    /*
     * R#23 offsets the line counter: R#19=20 and R#23=5 match visible
     * counter line 15.
     */
    write_control_register(&vdp, 19, 20);
    write_control_register(&vdp, 23, 5);
    match_tick =
        (DEFAULT_LINE_ZERO + 15) * TICKS_PER_LINE +
        DEFAULT_RIGHT_BORDER;
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, match_tick - 1);
    assert(!vdp.irq);
    vdp_advance(&vdp, 1);
    assert(vdp.irq);
    assert(vdp_read_status(&vdp) & 0x01);

    /* Match timing scales to a real CPU-cycle frame budget. */
    {
        const unsigned cpu_frame_cycles = 71590;
        unsigned match_cycle =
            (unsigned)(((u64)match_tick * cpu_frame_cycles +
                        PAL_FRAME_TICKS - 1) /
                       PAL_FRAME_TICKS);

        vdp_begin_frame(&vdp, cpu_frame_cycles, PAL_LINES);
        vdp_advance(&vdp, match_cycle - 1);
        assert(!vdp.irq);
        vdp_advance(&vdp, 1);
        assert(vdp.irq);
        assert(vdp_read_status(&vdp) & 0x01);
    }

    /*
     * On NTSC, high counter values can wrap into the next frame before
     * the top-border counter reset. Matches after that reset do not occur.
     */
    {
        const unsigned ntsc_lines = 262;
        const unsigned ntsc_frame_ticks = TICKS_PER_LINE * ntsc_lines;
        const unsigned ntsc_line_zero = 16 + 9 + 10 + 7;

        write_control_register(&vdp, 19, 230);
        write_control_register(&vdp, 23, 0);
        match_tick =
            (ntsc_line_zero + 230 - ntsc_lines) * TICKS_PER_LINE +
            DEFAULT_RIGHT_BORDER;
        vdp_begin_frame(&vdp, ntsc_frame_ticks, ntsc_lines);
        vdp_advance(&vdp, match_tick);
        assert(vdp.irq);
        assert(vdp_read_status(&vdp) & 0x01);

        write_control_register(&vdp, 19, 240);
        vdp_begin_frame(&vdp, ntsc_frame_ticks, ntsc_lines);
        vdp_advance(&vdp, ntsc_frame_ticks - 1);
        assert(!vdp.irq);
        assert(!(vdp_read_status(&vdp) & 0x01));
    }

    /*
     * With IE1 disabled FH is a beam-position pulse rather than a
     * latched IRQ. It spans the right border through the following
     * left border in a graphics mode.
     */
    write_control_register(&vdp, 0, 0);
    write_control_register(&vdp, 19, 0);
    write_control_register(&vdp, 23, 0);
    match_tick =
        DEFAULT_LINE_ZERO * TICKS_PER_LINE + DEFAULT_RIGHT_BORDER;
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, match_tick);
    assert(vdp_read_status(&vdp) & 0x01);
    assert(vdp_read_status(&vdp) & 0x01);
    assert(!vdp.irq);
    vdp_advance(&vdp, 287);
    assert(vdp_read_status(&vdp) & 0x01);
    vdp_advance(&vdp, 1);
    assert(!(vdp_read_status(&vdp) & 0x01));
}

static void test_v9938_interrupt_arbitration(void) {
    enum {
        TICKS_PER_LINE = 1368,
        PAL_LINES = 313,
        PAL_FRAME_TICKS = TICKS_PER_LINE * PAL_LINES,
        DEFAULT_LINE_ZERO = 16 + 36 + 10 + 7,
        DEFAULT_RIGHT_BORDER = 1282,
    };
    MsxVdp vdp;
    unsigned match_tick =
        DEFAULT_LINE_ZERO * TICKS_PER_LINE + DEFAULT_RIGHT_BORDER;

    vdp_init(&vdp);
    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);
    write_control_register(&vdp, 0, 0x10); /* IE1 */
    write_control_register(&vdp, 1, 0x20); /* IE0 */
    write_control_register(&vdp, 15, 0);
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, match_tick);
    assert(vdp.irq);
    vdp_end_frame(&vdp);

    /* Clearing F through S#0 must leave a pending FH interrupt active. */
    assert(vdp_read_status(&vdp) & 0x80);
    assert(vdp.irq);
    write_control_register(&vdp, 15, 1);
    assert(vdp_read_status(&vdp) & 0x01);
    assert(!vdp.irq);

    /* Conversely, acknowledging FH must not drop a pending vertical IRQ. */
    write_control_register(&vdp, 15, 0);
    vdp_begin_frame(&vdp, PAL_FRAME_TICKS, PAL_LINES);
    vdp_advance(&vdp, match_tick);
    vdp_end_frame(&vdp);
    write_control_register(&vdp, 15, 1);
    assert(vdp_read_status(&vdp) & 0x01);
    assert(vdp.irq);
    write_control_register(&vdp, 15, 0);
    assert(vdp_read_status(&vdp) & 0x80);
    assert(!vdp.irq);

    /* Enabling IE0 while F is already set asserts the shared IRQ line. */
    write_control_register(&vdp, 1, 0);
    vdp_end_frame(&vdp);
    assert(!vdp.irq);
    write_control_register(&vdp, 1, 0x20);
    assert(vdp.irq);
    write_control_register(&vdp, 1, 0);
    assert(!vdp.irq);
}

static void test_v9938_palette_and_indirect_register_port(void) {
    MsxVdp vdp;

    vdp_init(&vdp);
    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);
    assert(vdp.palette_grb[2] == 0x611);

    write_control_register(&vdp, 16, 2);
    vdp_write_palette(&vdp, 0x17);
    vdp_write_palette(&vdp, 0x03);
    assert(vdp.palette_grb[2] == 0x317);
    assert(vdp.registers[16] == 3);
    write_control_register(&vdp, 7, 2);
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 0) == 0x246dff);

    /* Writing R16 aborts a half-complete palette write. */
    vdp_write_palette(&vdp, 0x11);
    assert(vdp.palette_pending);
    write_control_register(&vdp, 16, 2);
    assert(!vdp.palette_pending);
    vdp_write_palette(&vdp, 0x22);
    assert(vdp.palette_pending);
    assert(vdp.palette_grb[2] == 0x317);

    vdp_reset(&vdp);
    write_control_register(&vdp, 17, 14);
    vdp_write_indirect(&vdp, 5);
    assert(vdp.registers[14] == 5);
    assert(vdp.registers[17] == 15);
    vdp_write_indirect(&vdp, 9);
    assert(vdp.registers[15] == 9);
    assert(vdp.registers[17] == 16);

    write_control_register(&vdp, 17, 0x8e);
    vdp_write_indirect(&vdp, 3);
    assert(vdp.registers[14] == 3);
    assert(vdp.registers[17] == 0x8e);
}

static void test_v9938_banked_and_planar_vram(void) {
    MsxVdp vdp;

    vdp_init(&vdp);
    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);

    write_control_register(&vdp, 14, 1);
    set_vram_address(&vdp, 0, true);
    vdp_write_data(&vdp, 0x11);
    write_control_register(&vdp, 14, 2);
    set_vram_address(&vdp, 0, true);
    vdp_write_data(&vdp, 0x22);
    assert(vdp.vram[0x4000] == 0x11);
    assert(vdp.vram[0x8000] == 0x22);
    write_control_register(&vdp, 14, 1);
    set_vram_address(&vdp, 0, false);
    assert(vdp_read_data(&vdp) == 0x11);

    /* Compatible modes wrap the 14-bit pointer within the selected bank. */
    vdp_reset(&vdp);
    write_control_register(&vdp, 14, 1);
    set_vram_address(&vdp, 0x3fff, true);
    vdp_write_data(&vdp, 0xaa);
    vdp_write_data(&vdp, 0xbb);
    assert(vdp.vram[0x7fff] == 0xaa);
    assert(vdp.vram[0x4000] == 0xbb);
    assert(vdp.registers[14] == 1);

    /* V9938 display modes carry a pointer wrap into R14. */
    vdp_reset(&vdp);
    write_control_register(&vdp, 0, 0x04); /* Graphics 3 */
    write_control_register(&vdp, 14, 7);
    set_vram_address(&vdp, 0x3fff, true);
    vdp_write_data(&vdp, 0xcc);
    vdp_write_data(&vdp, 0xdd);
    assert(vdp.vram[0x1ffff] == 0xcc);
    assert(vdp.vram[0] == 0xdd);
    assert(vdp.registers[14] == 0);

    /* Graphics 6/7 CPU accesses alternate between the two VRAM planes. */
    vdp_reset(&vdp);
    write_control_register(&vdp, 0, 0x0a); /* Graphics 6 */
    set_vram_address(&vdp, 0, true);
    vdp_write_data(&vdp, 0x12);
    vdp_write_data(&vdp, 0x34);
    assert(vdp.vram[0] == 0x12);
    assert(vdp.vram[0x10000] == 0x34);

    /* A TMS9918 remains restricted to its 16 KiB address space. */
    vdp_init(&vdp);
    set_vram_address(&vdp, 0x3fff, true);
    vdp_write_data(&vdp, 0x56);
    vdp_write_data(&vdp, 0x78);
    assert(vdp.vram[0x3fff] == 0x56);
    assert(vdp.vram[0] == 0x78);
    assert(vdp.vram[0x4000] == 0);
}

static void setup_v9938_bitmap(MsxVdp *vdp, u8 reg0) {
    vdp_init(vdp);
    vdp_set_type(vdp, MSX_VDP_V9938);
    vdp_reset(vdp);
    vdp->registers[0] = reg0;
    vdp->registers[1] = 0x40;
    vdp->registers[2] = 0x1f;
    vdp->registers[8] = 0x20; /* Disable colour-zero transparency. */
}

static void write_command_word(MsxVdp *vdp, unsigned low_reg,
                               unsigned value) {
    write_control_register(vdp, low_reg, (u8)value);
    write_control_register(vdp, low_reg + 1, (u8)(value >> 8));
}

static void test_v9938_bitmap_rendering(void) {
    MsxVdp vdp;

    /* SCREEN 5 stores two palette indices in each byte. */
    setup_v9938_bitmap(&vdp, 0x06);
    vdp.vram[0] = 0x24;
    vdp_render(&vdp);
    assert(vdp.render_width == 256);
    assert(vdp.render_height == 192);
    assert(pixel(&vdp, 0, 0) == 0x24db24);
    assert(pixel(&vdp, 1, 0) == 0x2424ff);

    /* R2 selects one of the 32 KiB SCREEN 5/6 display pages. */
    vdp.registers[2] = 0x3f;
    vdp.vram[0x8000] = 0xf2;
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 0) == COLOUR_WHITE);
    assert(pixel(&vdp, 1, 0) == 0x24db24);

    /* SCREEN 6 has four 2-bit, 512-dot pixels per byte. */
    setup_v9938_bitmap(&vdp, 0x08);
    vdp.vram[0] = 0x1b;
    vdp_render(&vdp);
    assert(vdp.render_width == 512);
    assert(pixel(&vdp, 0, 0) == 0x000000);
    assert(pixel(&vdp, 1, 0) == 0x000000);
    assert(pixel(&vdp, 2, 0) == 0x24db24);
    assert(pixel(&vdp, 3, 0) == 0x6dff6d);

    /* Transparent zero uses the two halves of R7 on alternating dots. */
    vdp.registers[8] = 0;
    vdp.registers[7] = 0x09;
    vdp.vram[0] = 0;
    vdp_render(&vdp);
    assert(pixel(&vdp, 0, 0) == 0x24db24);
    assert(pixel(&vdp, 1, 0) == 0x000000);

    /* SCREEN 7 alternates its packed bytes between the two VRAM planes. */
    setup_v9938_bitmap(&vdp, 0x0a);
    vdp.vram[0] = 0x24;
    vdp.vram[0x10000] = 0x6f;
    vdp_render(&vdp);
    assert(vdp.render_width == 512);
    assert(pixel(&vdp, 0, 0) == 0x24db24);
    assert(pixel(&vdp, 1, 0) == 0x2424ff);
    assert(pixel(&vdp, 2, 0) == 0xb62424);
    assert(pixel(&vdp, 3, 0) == COLOUR_WHITE);

    /* SCREEN 8 uses the fixed 256-colour GRB representation. */
    setup_v9938_bitmap(&vdp, 0x0e);
    vdp.vram[0] = 0x1c;
    vdp.vram[0x10000] = 0xe0;
    vdp.vram[1] = 0x03;
    vdp.registers[9] = 0x80;
    vdp_render(&vdp);
    assert(vdp.render_width == 256);
    assert(vdp.render_height == 212);
    assert(pixel(&vdp, 0, 0) == 0xff0000);
    assert(pixel(&vdp, 1, 0) == 0x00ff00);
    assert(pixel(&vdp, 2, 0) == 0x0000ff);
}

static void test_v9938_pixel_commands(void) {
    MsxVdp vdp;

    setup_v9938_bitmap(&vdp, 0x06); /* SCREEN 5 */

    /* PSET and POINT share the packed-pixel coordinate layout. */
    write_command_word(&vdp, 36, 1);
    write_command_word(&vdp, 38, 0);
    write_control_register(&vdp, 44, 6);
    write_control_register(&vdp, 46, 0x50);
    assert(vdp.vram[0] == 0x06);
    assert(!(vdp.status2 & 0x01));

    write_command_word(&vdp, 32, 1);
    write_command_word(&vdp, 34, 0);
    write_control_register(&vdp, 46, 0x40);
    write_control_register(&vdp, 15, 7);
    assert(vdp_read_status(&vdp) == 6);

    /* XOR is one of the five V9938 logical operations. */
    write_control_register(&vdp, 44, 3);
    write_control_register(&vdp, 46, 0x53);
    write_control_register(&vdp, 46, 0x40);
    assert(vdp_read_status(&vdp) == 5);

    /* SRCH reports the matching X coordinate and latches BD in S#2. */
    write_command_word(&vdp, 32, 0);
    write_control_register(&vdp, 44, 5);
    write_control_register(&vdp, 45, 0);
    write_control_register(&vdp, 46, 0x60);
    write_control_register(&vdp, 15, 2);
    assert(vdp_read_status(&vdp) & 0x10);
    write_control_register(&vdp, 15, 8);
    assert(vdp_read_status(&vdp) == 1);
    write_control_register(&vdp, 15, 9);
    assert(vdp_read_status(&vdp) == 0xfe);
    write_control_register(&vdp, 15, 2);
    assert(!(vdp_read_status(&vdp) & 0x10));

    /* LINE's NX is the major-axis distance, so NX=3 draws four dots. */
    write_command_word(&vdp, 36, 8);
    write_command_word(&vdp, 38, 1);
    write_command_word(&vdp, 40, 3);
    write_command_word(&vdp, 42, 0);
    write_control_register(&vdp, 44, 2);
    write_control_register(&vdp, 45, 0);
    write_control_register(&vdp, 46, 0x70);
    vdp_render(&vdp);
    for (int x = 8; x < 12; ++x)
        assert(pixel(&vdp, x, 1) == 0x24db24);
}

static void test_v9938_block_commands(void) {
    MsxVdp vdp;

    setup_v9938_bitmap(&vdp, 0x06); /* SCREEN 5 */

    /* LMMV fills a pixel rectangle. */
    write_command_word(&vdp, 36, 4);
    write_command_word(&vdp, 38, 2);
    write_command_word(&vdp, 40, 3);
    write_command_word(&vdp, 42, 2);
    write_control_register(&vdp, 44, 3);
    write_control_register(&vdp, 45, 0);
    write_control_register(&vdp, 46, 0x80);
    vdp_render(&vdp);
    for (int y = 2; y < 4; ++y)
        for (int x = 4; x < 7; ++x)
            assert(pixel(&vdp, x, y) == 0x6dff6d);

    /* LMMM copies the rectangle at pixel granularity. */
    write_command_word(&vdp, 32, 4);
    write_command_word(&vdp, 34, 2);
    write_command_word(&vdp, 36, 10);
    write_command_word(&vdp, 38, 6);
    write_control_register(&vdp, 46, 0x90);
    vdp_render(&vdp);
    for (int y = 6; y < 8; ++y)
        for (int x = 10; x < 13; ++x)
            assert(pixel(&vdp, x, y) == 0x6dff6d);

    /* HMMV/HMMM operate on complete packed bytes. */
    write_command_word(&vdp, 36, 0);
    write_command_word(&vdp, 38, 10);
    write_command_word(&vdp, 40, 4);
    write_command_word(&vdp, 42, 1);
    write_control_register(&vdp, 44, 0xa5);
    write_control_register(&vdp, 46, 0xc0);
    assert(vdp.vram[10 * 128] == 0xa5);
    assert(vdp.vram[10 * 128 + 1] == 0xa5);

    write_command_word(&vdp, 32, 0);
    write_command_word(&vdp, 34, 10);
    write_command_word(&vdp, 36, 0);
    write_command_word(&vdp, 38, 11);
    write_control_register(&vdp, 46, 0xd0);
    assert(vdp.vram[11 * 128] == 0xa5);
    assert(vdp.vram[11 * 128 + 1] == 0xa5);

    /* YMMM copies the rest of each scanline using DX for both sides. */
    write_command_word(&vdp, 34, 10);
    write_command_word(&vdp, 38, 12);
    write_control_register(&vdp, 46, 0xe0);
    assert(vdp.vram[12 * 128] == 0xa5);
    assert(vdp.vram[12 * 128 + 1] == 0xa5);
}

static void test_v9938_command_transfers(void) {
    MsxVdp vdp;

    setup_v9938_bitmap(&vdp, 0x06); /* SCREEN 5 */

    /*
     * A colour written before LMMC starts is a pending CPU transfer.
     * The MSX2 Sub-ROM relies on this ordering for single-pixel writes.
     */
    write_command_word(&vdp, 36, 6);
    write_command_word(&vdp, 38, 1);
    write_command_word(&vdp, 40, 1);
    write_command_word(&vdp, 42, 1);
    write_control_register(&vdp, 44, 7);
    write_control_register(&vdp, 45, 0);
    write_control_register(&vdp, 46, 0xb0);
    assert(!(vdp.status2 & 0x81));
    assert(vdp.vram[128 + 3] == 0x70);

    /* The same preloaded transfer rule applies to packed-byte HMMC. */
    write_command_word(&vdp, 36, 0);
    write_command_word(&vdp, 38, 2);
    write_command_word(&vdp, 40, 2);
    write_command_word(&vdp, 42, 1);
    write_control_register(&vdp, 44, 0xab);
    write_control_register(&vdp, 46, 0xf0);
    assert(!(vdp.status2 & 0x81));
    assert(vdp.vram[2 * 128] == 0xab);

    /* HMMC consumes one R#44 byte for each packed destination byte. */
    write_command_word(&vdp, 36, 0);
    write_command_word(&vdp, 38, 0);
    write_command_word(&vdp, 40, 4);
    write_command_word(&vdp, 42, 1);
    write_control_register(&vdp, 45, 0);
    write_control_register(&vdp, 46, 0xf0);
    assert((vdp.status2 & 0x81) == 0x81);
    write_control_register(&vdp, 44, 0x12);
    assert((vdp.status2 & 0x81) == 0x81);
    write_control_register(&vdp, 44, 0x34);
    assert(!(vdp.status2 & 0x81));
    assert(vdp.vram[0] == 0x12);
    assert(vdp.vram[1] == 0x34);

    /* LMMC consumes one colour value per destination pixel. */
    write_command_word(&vdp, 36, 4);
    write_command_word(&vdp, 38, 0);
    write_command_word(&vdp, 40, 2);
    write_command_word(&vdp, 42, 1);
    write_control_register(&vdp, 46, 0xb0);
    write_control_register(&vdp, 44, 5);
    write_control_register(&vdp, 44, 6);
    assert(!(vdp.status2 & 0x81));
    assert(vdp.vram[2] == 0x56);

    /* LMCM exposes each source pixel through S#7. */
    write_command_word(&vdp, 32, 4);
    write_command_word(&vdp, 34, 0);
    write_command_word(&vdp, 40, 2);
    write_command_word(&vdp, 42, 1);
    write_control_register(&vdp, 46, 0xa0);
    assert((vdp.status2 & 0x81) == 0x81);
    write_control_register(&vdp, 15, 7);
    assert(vdp_read_status(&vdp) == 5);
    assert((vdp.status2 & 0x81) == 0x81);
    assert(vdp_read_status(&vdp) == 6);
    assert(!(vdp.status2 & 0x81));
}

int main(void) {
    test_basic_position_wrap_and_terminator();
    test_priority_transparency_and_collision();
    test_early_clock_and_clipping();
    test_size_and_magnification();
    test_four_sprite_limit_and_fifth_index();
    test_first_overflow_is_scanline_ordered();
    test_status_latching_and_vblank();
    test_sprite_modes_and_display_gating();
    test_v9938_sprite_mode2_attributes_and_limits();
    test_v9938_sprite_mode2_combining_and_collision();
    test_v9938_sprite_mode2_display_formats();
    test_graphics_2_and_multicolour_mode_bits();
    test_backdrop_and_text_background_colours();
    test_v9938_registers_and_status_selection();
    test_v9938_retrace_status();
    test_v9938_line_interrupt();
    test_v9938_interrupt_arbitration();
    test_v9938_palette_and_indirect_register_port();
    test_v9938_banked_and_planar_vram();
    test_v9938_bitmap_rendering();
    test_v9938_pixel_commands();
    test_v9938_block_commands();
    test_v9938_command_transfers();
    return 0;
}
