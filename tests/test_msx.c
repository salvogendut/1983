#include "msx.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_vdp_ppm_if_requested(const MsxVdp *vdp) {
    const char *path = getenv("MSX_CBIOS_PPM");
    FILE *file;

    if (!path || !path[0])
        return;
    file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", MSX1_VIDEO_W, MSX1_VIDEO_H);
    for (size_t i = 0; i < sizeof(vdp->pixels) / sizeof(vdp->pixels[0]);
         ++i) {
        u8 rgb[] = {
            (u8)(vdp->pixels[i] >> 16),
            (u8)(vdp->pixels[i] >> 8),
            (u8)vdp->pixels[i],
        };
        assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
    assert(fclose(file) == 0);
}

static void test_slot_bus_and_cpu(void) {
    MsxMachine *msx = malloc(sizeof(*msx));
    u8 bios[MSX_BIOS_SIZE];
    u8 cartridge[0x4000];

    assert(msx);
    memset(bios, 0xff, sizeof(bios));
    {
        const u8 program[] = {
            0x3e, 0xc0,       /* LD A,C0: slot 3 in page 3 */
            0xd3, 0xa8,       /* OUT (A8),A */
            0x3e, 0x5a,       /* LD A,5A */
            0x32, 0x00, 0xc0, /* LD (C000),A */
            0x3e, 0xf2,       /* select keyboard row 2 */
            0xd3, 0xaa,       /* OUT (AA),A */
            0xdb, 0xa9,       /* IN A,(A9) */
            0x32, 0x01, 0xc0, /* LD (C001),A */
            0x76,             /* HALT */
        };
        memcpy(bios, program, sizeof(program));
    }

    msx_init(msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_install_bios(msx, bios, sizeof(bios)) == 0);
    assert(msx_can_boot(msx));
    assert(msx_memory_read(msx, 0) == 0x3e);
    assert(msx_memory_read(msx, 0xc000) == 0xff);
    msx_keyboard_press(msx, 2, 6); /* A */

    msx_run_frame(msx);
    assert(msx->frame == 1);
    assert(msx->primary_slot == 0xc0);
    assert(msx->ram[0xc000] == 0x5a);
    assert(msx->ram[0xc001] == 0xbf);
    assert(msx->cpu.halted);
    assert(msx->cycles >= MSX_CPU_HZ / 50);
    assert(msx->instructions > 5);

    msx_reset(msx);
    assert(msx_can_boot(msx));
    assert(msx->ram[0xc000] == 0);
    assert(msx_keyboard_read_row(msx, 2) == 0xff);
    assert(msx->cpu.pc == 0);

    memset(cartridge, 0xff, sizeof(cartridge));
    cartridge[0] = 'A';
    cartridge[1] = 'B';
    cartridge[2] = 0x10;
    cartridge[3] = 0x40;
    assert(msx_install_cartridge(msx, cartridge, sizeof(cartridge)) == 0);
    msx_io_write(msx, 0xa8, 0x04);
    assert(msx_memory_read(msx, 0x4000) == 'A');
    assert(msx_memory_read(msx, 0x4001) == 'B');
    msx_memory_write(msx, 0x4000, 0);
    assert(msx_memory_read(msx, 0x4000) == 'A');
    free(msx);
}

static void test_msx2_expanded_slots_and_firmware(void) {
    MsxMachine *msx = malloc(sizeof(*msx));
    u8 subrom[MSX_SUBROM_SIZE];
    u8 disk_rom[MSX_DISK_ROM_SIZE];

    assert(msx);
    for (size_t i = 0; i < sizeof(subrom); ++i)
        subrom[i] = (u8)(i ^ 0x5a);
    for (size_t i = 0; i < sizeof(disk_rom); ++i)
        disk_rom[i] = (u8)(i ^ 0xa5);

    msx_init(msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_PAL, 128);
    assert(msx_install_subrom(msx, subrom, sizeof(subrom)) == 0);
    assert(msx_install_disk_rom(msx, disk_rom, sizeof(disk_rom)) == 0);
    assert(msx->subrom_loaded);
    assert(msx->disk_rom_loaded);
    assert(msx_install_subrom(msx, subrom, sizeof(subrom) - 1) < 0);
    assert(msx_install_disk_rom(msx, disk_rom, sizeof(disk_rom) - 1) < 0);

    /* NMS 8250 primary slot 3 is expanded. Its reset selection exposes
     * secondary slot 0, where the 16 KB Sub-ROM is mirrored on every page. */
    msx_io_write(msx, 0xa8, 0xff);
    assert(msx_memory_read(msx, 0x0000) == subrom[0]);
    assert(msx_memory_read(msx, 0x4123) == subrom[0x0123]);
    assert(msx_memory_read(msx, 0x9234) == subrom[0x1234]);
    assert(msx_memory_read(msx, 0xffff) == 0xff);

    /*
     * Select Sub-ROM, disk ROM, Sub-ROM, and mapper RAM for pages 0..3.
     * The expanded-slot register reads back inverted and supersedes the
     * underlying device at address FFFF.
     */
    msx_memory_write(msx, 0xffff, 0x8c);
    assert(msx->secondary_slot[3] == 0x8c);
    assert(msx_memory_read(msx, 0xffff) == 0x73);
    assert(msx_memory_read(msx, 0x0000) == subrom[0]);
    assert(msx_memory_read(msx, 0x4000) == disk_rom[0]);
    assert(msx_memory_read(msx, 0x7fff) == disk_rom[0x3fff]);
    assert(msx_memory_read(msx, 0x8000) == subrom[0]);
    msx_memory_write(msx, 0xc000, 0x44);
    assert(msx_memory_read(msx, 0xc000) == 0x44);

    /* The disk ROM is only visible in page 1 of secondary slot 3. */
    msx_memory_write(msx, 0xffff, 0x8f);
    assert(msx_memory_read(msx, 0x0000) == 0xff);
    assert(msx_memory_read(msx, 0x4000) == disk_rom[0]);

    /* Mapper ports FC..FF select one 16 KB segment for each CPU page. */
    msx_memory_write(msx, 0xffff, 0xaa);
    msx_io_write(msx, 0xfc, 0);
    msx_io_write(msx, 0xfd, 1);
    msx_io_write(msx, 0xfe, 2);
    msx_io_write(msx, 0xff, 3);
    assert(msx_io_read(msx, 0xfc) == 0xf8);
    assert(msx_io_read(msx, 0xfd) == 0xf9);
    assert(msx_io_read(msx, 0xfe) == 0xfa);
    assert(msx_io_read(msx, 0xff) == 0xfb);
    msx_memory_write(msx, 0x0000, 0x10);
    msx_memory_write(msx, 0x4000, 0x21);
    msx_memory_write(msx, 0x8000, 0x32);
    msx_memory_write(msx, 0xc000, 0x43);
    assert(msx->ram[0x0000] == 0x10);
    assert(msx->ram[0x4000] == 0x21);
    assert(msx->ram[0x8000] == 0x32);
    assert(msx->ram[0xc000] == 0x43);

    msx_io_write(msx, 0xfc, 0xff);
    assert(msx_io_read(msx, 0xfc) == 0xff);
    msx_memory_write(msx, 0x0000, 0x77);
    assert(msx->ram[0x1c000] == 0x77);

    msx_reset(msx);
    assert(msx->subrom_loaded);
    assert(msx->disk_rom_loaded);
    assert(msx->secondary_slot[3] == 0);
    assert(msx->mapper_segment[0] == 0);
    assert(msx->ram[0x1c000] == 0);
    free(msx);
}

static void test_vdp_ports_and_renderer(void) {
    MsxVdp vdp;

    vdp_init(&vdp);
    vdp_write_control(&vdp, 0x00);
    vdp_write_control(&vdp, 0x40);
    vdp_write_data(&vdp, 0xaa);
    assert(vdp.vram[0] == 0xaa);

    vdp_write_control(&vdp, 0x00);
    vdp_write_control(&vdp, 0x00);
    assert(vdp_read_data(&vdp) == 0xaa);

    vdp.registers[1] = 0x60;
    vdp.registers[2] = 0x00;
    vdp.registers[3] = 0x80;
    vdp.registers[4] = 0x01;
    vdp.registers[7] = 0x02;
    vdp.vram[0] = 1;
    vdp.vram[0x808] = 0x80;
    vdp.vram[0x2000] = 0xf0;
    vdp_end_frame(&vdp);
    assert(vdp.irq);
    assert(vdp.pixels[0] == 0xffffff);
    assert(vdp.pixels[1] == 0x3eb849);
    assert(vdp_read_status(&vdp) & 0x80);
    assert(!vdp.irq);
}

static void test_msx2_vdp_extended_ports(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_PAL, 128);
    assert(msx.vdp.type == MSX_VDP_V9938);

    /* R16 selects palette entry 4; port 9A writes its two GRB bytes. */
    msx_io_write(&msx, 0x99, 4);
    msx_io_write(&msx, 0x99, 0x90);
    msx_io_write(&msx, 0x9a, 0x26);
    msx_io_write(&msx, 0x9a, 0x04);
    assert(msx.vdp.palette_grb[4] == 0x426);
    assert(msx.vdp.registers[16] == 5);

    /* R17 selects R14; port 9B writes it and advances the selector. */
    msx_io_write(&msx, 0x99, 14);
    msx_io_write(&msx, 0x99, 0x91);
    msx_io_write(&msx, 0x9b, 6);
    assert(msx.vdp.registers[14] == 6);
    assert(msx.vdp.registers[17] == 15);
    assert(msx_io_read(&msx, 0x9a) == 0xff);
    assert(msx_io_read(&msx, 0x9b) == 0xff);

    /* On an MSX1 VDP, 9A/9B mirror the data/control ports. */
    msx_configure(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx.vdp.type == MSX_VDP_TMS9918);
    msx_io_write(&msx, 0x9b, 0x00);
    msx_io_write(&msx, 0x9b, 0x40);
    msx_io_write(&msx, 0x9a, 0xa5);
    assert(msx.vdp.vram[0] == 0xa5);
}

static void test_keyboard_matrix_and_ppi(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    for (unsigned row = 0; row < MSX_KEYBOARD_ROWS; ++row)
        assert(msx_keyboard_read_row(&msx, row) == 0xff);
    assert(msx_keyboard_read_row(&msx, MSX_KEYBOARD_ROWS) == 0xff);

    msx_keyboard_press(&msx, 2, 6); /* A */
    msx_keyboard_press(&msx, 2, 7); /* B */
    msx_io_write(&msx, 0xaa, 0xf2);
    assert(msx_io_read(&msx, 0xa9) == 0x3f);

    /* Reference counts keep a shared matrix position down until every
     * physical host alias has been released. */
    msx_keyboard_press(&msx, 6, 0);
    msx_keyboard_press(&msx, 6, 0);
    msx_io_write(&msx, 0xaa, 0xf6);
    assert(msx_io_read(&msx, 0xa9) == 0xfe);
    msx_keyboard_release(&msx, 6, 0);
    assert(msx_io_read(&msx, 0xa9) == 0xfe);
    msx_keyboard_release(&msx, 6, 0);
    assert(msx_io_read(&msx, 0xa9) == 0xff);

    /* PPI bit set/reset operations on port C also change the row select. */
    msx_io_write(&msx, 0xaa, 0xf0);
    assert(msx_io_read(&msx, 0xa9) == 0xff);
    msx_io_write(&msx, 0xab, 0x01); /* set port-C bit 0: row 1 */
    msx_keyboard_press(&msx, 1, 3);
    assert(msx_io_read(&msx, 0xa9) == 0xf7);
    msx_io_write(&msx, 0xaa, 0xfb);
    assert(msx_io_read(&msx, 0xa9) == 0xff);

    msx_keyboard_clear(&msx);
    for (unsigned row = 0; row < MSX_KEYBOARD_ROWS; ++row)
        assert(msx_keyboard_read_row(&msx, row) == 0xff);
}

static void test_psg_ports_and_cycle_timed_audio(void) {
    MsxMachine *msx = malloc(sizeof(*msx));
    u8 bios[MSX_BIOS_SIZE];
    bool positive = false;
    bool negative = false;

    assert(msx);
    msx_init(msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx->psg.variant == PSG_VARIANT_AY8910);

    msx_io_write(msx, 0xa0, 1);
    msx_io_write(msx, 0xa1, 0xff);
    assert(msx_io_read(msx, 0xa2) == 0x0f);
    msx_io_write(msx, 0xa0, 7);
    msx_io_write(msx, 0xa1, 0xff);
    assert(msx_io_read(msx, 0xa2) == 0xbf);

    msx_io_write(msx, 0xa0, 15);
    msx_io_write(msx, 0xa1, 0x00);
    assert(msx->kana_led);
    assert(msx_io_read(msx, 0xa2) == 0x00);
    msx_io_write(msx, 0xa1, 0x80);
    assert(!msx->kana_led);

    msx_io_write(msx, 0xa0, 14);
    msx_io_write(msx, 0xa1, 0x55);
    assert(msx_io_read(msx, 0xa2) == 0xbf);

    memset(bios, 0xff, sizeof(bios));
    {
        const u8 program[] = {
            0x3e, 0x00, 0xd3, 0xa0, /* tone A fine */
            0x3e, 0x20, 0xd3, 0xa1,
            0x3e, 0x01, 0xd3, 0xa0, /* tone A coarse */
            0xaf,       0xd3, 0xa1,
            0x3e, 0x07, 0xd3, 0xa0, /* A tone, no noise/B/C */
            0x3e, 0x3e, 0xd3, 0xa1,
            0x3e, 0x08, 0xd3, 0xa0, /* A volume */
            0x3e, 0x0f, 0xd3, 0xa1,
            0x76,
        };
        memcpy(bios, program, sizeof(program));
    }
    assert(msx_install_bios(msx, bios, sizeof(bios)) == 0);
    msx_run_frame(msx);
    assert(msx->audio_sample_count >= 880);
    assert(msx->audio_sample_count <= 884);
    assert(msx->audio_sample_cycles < MSX_CPU_HZ);
    for (size_t i = 0; i < msx->audio_sample_count; ++i) {
        positive |= msx->audio_samples[i] > 0;
        negative |= msx->audio_samples[i] < 0;
    }
    assert(positive);
    assert(negative);

    msx_configure(msx, MSX_MODEL_GENERIC_MSX2,
                  MSX_REGION_NTSC, 128);
    assert(msx->psg.variant == PSG_VARIANT_YM2149);
    free(msx);
}

static void test_cbios_checkpoint_if_available(void) {
    const char *directory = getenv("MSX_CBIOS_DIR");
    MsxMachine *msx;
    char main_path[4096];
    char logo_path[4096];
    size_t nonzero_vram = 0;
    u8 cartridge[0x4000];

    if (!directory || !directory[0])
        return;
    snprintf(main_path, sizeof(main_path), "%s/cbios_main_msx1.rom",
             directory);
    snprintf(logo_path, sizeof(logo_path), "%s/cbios_logo_msx1.rom",
             directory);

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_NTSC, 64);
    assert(msx_load_bios(msx, main_path) == 0);
    assert(msx_load_logo(msx, logo_path) == 0);
    for (int frame = 0; frame < 180; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;

    fprintf(stderr,
            "C-BIOS checkpoint: frame=%llu PC=%04X SP=%04X slot=%02X "
            "cycles=%llu instructions=%llu VRAM=%zu R0=%02X R1=%02X\n",
            (unsigned long long)msx->frame, msx->cpu.pc, msx->cpu.sp,
            msx->primary_slot, (unsigned long long)msx->cycles,
            (unsigned long long)msx->instructions, nonzero_vram,
            msx->vdp.registers[0], msx->vdp.registers[1]);
    assert(msx->frame == 180);
    assert(msx->cycles >= (u64)MSX_CPU_HZ * 179 / 60);
    assert(msx->instructions > 100000);
    assert(nonzero_vram > 100);
    assert(msx->cpu.pc == 0x1a65);
    assert(msx->cpu.sp == 0xf300);
    assert(msx->primary_slot == 0xf0);
    assert(msx->vdp.registers[0] == 0x00);
    assert(msx->vdp.registers[1] == 0xe0);
    assert(nonzero_vram == 5692);
    write_vdp_ppm_if_requested(&msx->vdp);

    memset(cartridge, 0, sizeof(cartridge));
    cartridge[0] = 'A';
    cartridge[1] = 'B';
    cartridge[2] = 0x10;
    cartridge[3] = 0x40;
    {
        const u8 program[] = {
            0xf3,             /* DI */
            0x3e, 0x00,
            0xd3, 0x99,       /* VDP address low */
            0x3e, 0x40,
            0xd3, 0x99,       /* VRAM write command */
            0x3e, 0xa5,
            0xd3, 0x98,       /* write sentinel */
            0xc3, 0x10, 0x40, /* loop */
        };
        memcpy(cartridge + 0x10, program, sizeof(program));
    }
    assert(msx_install_cartridge(msx, cartridge, sizeof(cartridge)) == 0);
    for (int frame = 0; frame < 180; ++frame)
        msx_run_frame(msx);
    assert(msx->cartridge_loaded);
    assert(msx->vdp.vram[0] == 0xa5);
    assert(msx->cpu.pc >= 0x4010 && msx->cpu.pc < 0x4020);
    free(msx);
}

static void test_nms8250_checkpoint_if_available(void) {
    const char *directory = getenv("MSX_NMS8250_DIR");
    MsxMachine *msx;
    char bios_path[4096];
    char subrom_path[4096];
    char disk_rom_path[4096];
    size_t nonzero_vram = 0;

    if (!directory || !directory[0])
        return;
    snprintf(bios_path, sizeof(bios_path),
             "%s/nms8250_basic-bios2.rom", directory);
    snprintf(subrom_path, sizeof(subrom_path),
             "%s/nms8250_msx2sub.rom", directory);
    snprintf(disk_rom_path, sizeof(disk_rom_path),
             "%s/nms8250_disk.rom", directory);

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_PAL, 128);
    assert(msx_load_bios(msx, bios_path) == 0);
    assert(msx_load_subrom(msx, subrom_path) == 0);
    assert(msx_load_disk_rom(msx, disk_rom_path) == 0);
    for (int frame = 0; frame < 200; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;

    fprintf(stderr,
            "NMS 8250 checkpoint: frame=%llu PC=%04X SP=%04X slot=%02X "
            "subslot=%02X mapper=%02X,%02X,%02X,%02X "
            "cycles=%llu instructions=%llu VRAM=%zu "
            "R0=%02X R1=%02X R9=%02X R14=%02X\n",
            (unsigned long long)msx->frame, msx->cpu.pc, msx->cpu.sp,
            msx->primary_slot, msx->secondary_slot[3],
            msx->mapper_segment[0], msx->mapper_segment[1],
            msx->mapper_segment[2], msx->mapper_segment[3],
            (unsigned long long)msx->cycles,
            (unsigned long long)msx->instructions, nonzero_vram,
            msx->vdp.registers[0], msx->vdp.registers[1],
            msx->vdp.registers[9], msx->vdp.registers[14]);
    assert(msx->frame == 200);
    assert(msx->bios_loaded);
    assert(msx->subrom_loaded);
    assert(msx->disk_rom_loaded);
    assert(msx->primary_slot == 0xf3);
    assert(msx->secondary_slot[3] == 0xa0);
    assert(msx->mapper_segment[0] == 3);
    assert(msx->mapper_segment[1] == 2);
    assert(msx->mapper_segment[2] == 1);
    assert(msx->mapper_segment[3] == 0);
    assert(msx->instructions > 1000000);
    assert(msx->vdp.type == MSX_VDP_V9938);
    assert(msx->vdp.registers[9] == 0x02);
    free(msx);
}

int main(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(strcmp(msx.profile->name, "Generic MSX1") == 0);
    assert(strcmp(msx_vdp_name(&msx), "TMS9929A") == 0);
    assert(msx.profile->vram_kb == 16);
    assert(!msx.profile->expanded_slots);
    assert(!msx.profile->memory_mapper);
    assert(!msx.profile->rtc);
    assert(msx.frame_hz == 50);

    msx_run_frame(&msx);
    assert(msx.frame == 1);
    msx.paused = true;
    msx_run_frame(&msx);
    assert(msx.frame == 1);

    msx_configure(&msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_NTSC, 200);
    assert(strcmp(msx_vdp_name(&msx), "V9938") == 0);
    assert(msx.ram_kb == 128);
    assert(msx.profile->vram_kb == 128);
    assert(msx.profile->expanded_slots);
    assert(msx.profile->memory_mapper);
    assert(msx.profile->rtc);
    assert(msx.frame_hz == 60);
    assert(msx.frame == 0);

    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX1, 64, 1) == 16);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX1, 16, -1) == 64);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX2, 128, 1) == 64);

    test_slot_bus_and_cpu();
    test_msx2_expanded_slots_and_firmware();
    test_vdp_ports_and_renderer();
    test_msx2_vdp_extended_ports();
    test_keyboard_matrix_and_ppi();
    test_psg_ports_and_cycle_timed_audio();
    test_cbios_checkpoint_if_available();
    test_nms8250_checkpoint_if_available();
    return 0;
}
