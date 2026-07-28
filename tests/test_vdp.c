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
    test_v9938_registers_and_status_selection();
    test_v9938_palette_and_indirect_register_port();
    test_v9938_banked_and_planar_vram();
    return 0;
}
