#include "vdp.h"

#include <assert.h>

#define SPRITE_ATTRIBUTE_BASE 0x1b00u
#define SPRITE_PATTERN_BASE   0x3000u

#define COLOUR_BACKDROP 0x000000u
#define COLOUR_GREEN    0x3eb849u
#define COLOUR_LT_GREEN 0x74d07du
#define COLOUR_BLUE     0x5955e0u
#define COLOUR_LT_BLUE  0x8076f1u
#define COLOUR_DK_RED   0xb95e51u
#define COLOUR_LT_RED   0xff897du
#define COLOUR_WHITE    0xffffffu

static u32 pixel(const MsxVdp *vdp, int x, int y) {
    return vdp->pixels[y * MSX1_VIDEO_W + x];
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

int main(void) {
    test_basic_position_wrap_and_terminator();
    test_priority_transparency_and_collision();
    test_early_clock_and_clipping();
    test_size_and_magnification();
    test_four_sprite_limit_and_fifth_index();
    test_first_overflow_is_scanline_ordered();
    test_status_latching_and_vblank();
    test_sprite_modes_and_display_gating();
    test_graphics_2_and_multicolour_mode_bits();
    test_backdrop_and_text_background_colours();
    return 0;
}
