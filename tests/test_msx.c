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

static void write_fixture(const char *path, const u8 *data, size_t size) {
    FILE *file = fopen(path, "wb");

    assert(file);
    assert(fwrite(data, 1, size, file) == size);
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
    msx_destroy(msx);
    free(msx);
}

static void test_dual_cartridge_slots_and_mapper_reset(void) {
    MsxMachine msx;
    u8 ascii8[0x8000];
    u8 ascii16[0x10000];

    for (unsigned bank = 0; bank < 4; ++bank) {
        memset(ascii8 + bank * 0x2000, (int)(0x10 + bank), 0x2000);
        memset(ascii16 + bank * 0x4000, (int)(0x20 + bank), 0x4000);
    }
    ascii8[0] = ascii16[0] = 'A';
    ascii8[1] = ascii16[1] = 'B';

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_install_cartridge_slot(
               &msx, 0, ascii8, sizeof(ascii8),
               MSX_CART_MAPPER_ASCII8) == 0);
    assert(msx_install_cartridge_slot(
               &msx, 1, ascii16, sizeof(ascii16),
               MSX_CART_MAPPER_ASCII16) == 0);
    assert(msx_get_cartridge(&msx, 0)->loaded);
    assert(msx_get_cartridge(&msx, 1)->loaded);

    /* Primary slot 1 in pages 1/2 exposes cartridge 1. */
    msx_io_write(&msx, 0xa8, 0x14);
    msx_memory_write(&msx, 0x6000, 1);
    msx_memory_write(&msx, 0x7000, 2);
    assert(msx_memory_read(&msx, 0x4004) == 0x11);
    assert(msx_memory_read(&msx, 0x8004) == 0x12);

    /* Primary slot 2 in pages 1/2 independently exposes cartridge 2. */
    msx_io_write(&msx, 0xa8, 0x28);
    msx_memory_write(&msx, 0x6000, 2);
    msx_memory_write(&msx, 0x7000, 3);
    assert(msx_memory_read(&msx, 0x4004) == 0x22);
    assert(msx_memory_read(&msx, 0x8004) == 0x23);

    msx_reset(&msx);
    msx_io_write(&msx, 0xa8, 0x14);
    assert(msx_memory_read(&msx, 0x4004) == 0x10);
    assert(msx_memory_read(&msx, 0x8004) == 0x10);
    msx_io_write(&msx, 0xa8, 0x28);
    assert(msx_memory_read(&msx, 0x4004) == 0x20);
    assert(msx_memory_read(&msx, 0x8004) == 0x20);

    msx_eject_cartridge(&msx, 1);
    msx_io_write(&msx, 0xa8, 0x28);
    assert(msx_memory_read(&msx, 0x4000) == 0xff);
    assert(!msx_get_cartridge(&msx, 1)->loaded);
    assert(msx_get_cartridge(&msx, 0)->loaded);
    msx_destroy(&msx);
}

static void test_sunrise_cartridge_slot_bus(void) {
    MsxMachine msx;
    u8 rom[MSX_SUNRISE_ROM_SIZE];

    for (unsigned bank = 0; bank < 8; ++bank)
        memset(&rom[bank * MSX_SUNRISE_BANK_SIZE],
               (int)(0x80 + bank), MSX_SUNRISE_BANK_SIZE);
    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(!msx_sunrise_connected(&msx));
    assert(msx_install_sunrise_ide(
               &msx, 1, rom, sizeof(rom)) == 0);
    assert(msx_sunrise_connected(&msx));
    assert(msx_sunrise_slot(&msx) == 1);

    /* Select primary slot 2 for page 1, where the Sunrise ROM lives. */
    msx_io_write(&msx, 0xa8, 0x08);
    assert(msx_memory_read(&msx, 0x4000) == 0x87);
    msx_memory_write(&msx, 0x4104, 0x81);
    assert(msx_memory_read(&msx, 0x4000) == 0x81);
    assert(msx_memory_read(&msx, 0x7e07) == 0x7f);

    msx_reset(&msx);
    assert(msx_sunrise_connected(&msx));
    assert(msx_sunrise_slot(&msx) == 1);
    msx_io_write(&msx, 0xa8, 0x08);
    assert(msx_memory_read(&msx, 0x4000) == 0x81);

    msx_eject_sunrise_ide(&msx);
    assert(!msx_sunrise_connected(&msx));
    assert(msx_sunrise_slot(&msx) == -1);
    assert(msx_memory_read(&msx, 0x4000) == 0xff);
    msx_destroy(&msx);
}

static void test_sd_mapper_expanded_cartridge_bus(void) {
    MsxMachine msx;
    u8 *rom = malloc(MSX_SD_MAPPER_ROM_SIZE);

    assert(rom);
    for (unsigned bank = 0;
         bank < MSX_SD_MAPPER_ROM_SIZE /
                    MSX_SD_MAPPER_ROM_BANK_SIZE;
         ++bank) {
        memset(rom + bank * MSX_SD_MAPPER_ROM_BANK_SIZE,
               0x40 + bank, MSX_SD_MAPPER_ROM_BANK_SIZE);
    }
    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_install_sd_mapper(
               &msx, 0, rom, MSX_SD_MAPPER_ROM_SIZE) == 0);
    assert(msx_sd_mapper_connected(&msx));
    assert(msx_sd_mapper_slot(&msx) == 0);

    /* The cartridge expands primary slot 1; subslot 0 is its driver. */
    msx_io_write(&msx, 0xa8, 0x55);
    assert(msx_memory_read(&msx, 0xffff) == 0xff);
    assert(msx_memory_read(&msx, 0x4000) == 0x40);
    msx_memory_write(&msx, 0x6000, 6);
    assert(msx_memory_read(&msx, 0x4000) == 0x46);

    /* Subslot 1 is the cartridge's independent 512 KiB mapper. */
    msx_memory_write(&msx, 0xffff, 0x55);
    assert(msx_memory_read(&msx, 0xffff) == 0xaa);
    assert(msx_io_read(&msx, 0xfc) == 0xe3);
    msx_io_write(&msx, 0xfc, 31);
    msx_memory_write(&msx, 0x0000, 0xa5);
    assert(msx_memory_read(&msx, 0x0000) == 0xa5);
    msx_io_write(&msx, 0xfc, 30);
    assert(msx_memory_read(&msx, 0x0000) == 0);
    msx_io_write(&msx, 0xfc, 31);
    assert(msx_memory_read(&msx, 0x0000) == 0xa5);

    msx_reset(&msx);
    assert(msx_sd_mapper_connected(&msx));
    msx_io_write(&msx, 0xa8, 0x55);
    assert(msx_memory_read(&msx, 0xffff) == 0xff);
    assert(msx_io_read(&msx, 0xfc) == 0xe3);
    assert(msx_eject_sd_mapper(&msx) == 0);
    assert(!msx_sd_mapper_connected(&msx));
    msx_destroy(&msx);
    free(rom);
}

static void test_megaflash_expanded_cartridge_bus(void) {
    MsxMachine msx;
    u8 *flash = malloc(MSX_MEGAFLASH_FLASH_SIZE);

    assert(flash);
    for (size_t address = 0;
         address < MSX_MEGAFLASH_FLASH_SIZE; ++address)
        flash[address] = (u8)(address >> 13);
    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_NTSC, 64);
    assert(msx_install_megaflash(
               &msx, 0, flash, MSX_MEGAFLASH_FLASH_SIZE) == 0);
    assert(msx_megaflash_connected(&msx));
    assert(msx_megaflash_slot(&msx) == 0);
    msx.primary_slot = 0x55;
    assert(msx_memory_read(&msx, 0x4000) == 0);
    assert(msx_memory_read(&msx, 0xffff) == 0xff);
    msx_memory_write(&msx, 0xffff, 0x55);
    assert(msx_memory_read(&msx, 0xffff) == 0xaa);
    assert(msx_memory_read(&msx, 0x4000) == 8);
    assert(msx_io_read(&msx, 0xfc) == 0xe3);
    msx_io_write(&msx, 0xfc, 12);
    assert(msx_io_read(&msx, 0xfc) == 0xec);
    msx_io_write(&msx, 0x10, 8);
    msx_io_write(&msx, 0x11, 15);
    assert(msx.megaflash.psg.registers[8] == 15);
    assert(msx_eject_megaflash(&msx) == 0);
    assert(!msx_megaflash_connected(&msx));
    msx_destroy(&msx);
    free(flash);
}

static void test_ascii8_cpu_boot_checkpoint(void) {
    MsxMachine msx;
    u8 bios[MSX_BIOS_SIZE];
    u8 cartridge[0x8000];
    const u8 bios_program[] = {
        0x3e, 0xd4,       /* slot 1 pages 1/2, RAM in page 3 */
        0xd3, 0xa8,       /* OUT (A8),A */
        0xc3, 0x10, 0x60, /* JP 6010 */
    };
    const u8 cartridge_program[] = {
        0x3e, 0x03,       /* LD A,3 */
        0x32, 0x00, 0x70, /* map bank 3 into 8000-9FFF */
        0x3a, 0x04, 0x80, /* LD A,(8004) */
        0x32, 0x00, 0xc0, /* LD (C000),A */
        0x76,             /* HALT */
    };

    memset(bios, 0xff, sizeof(bios));
    memcpy(bios, bios_program, sizeof(bios_program));
    for (unsigned bank = 0; bank < 4; ++bank)
        memset(cartridge + bank * 0x2000, (int)(0x50 + bank), 0x2000);
    cartridge[0] = 'A';
    cartridge[1] = 'B';
    memcpy(cartridge + 0x10, cartridge_program,
           sizeof(cartridge_program));

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_install_bios(&msx, bios, sizeof(bios)) == 0);
    assert(msx_install_cartridge_slot(
               &msx, 0, cartridge, sizeof(cartridge),
               MSX_CART_MAPPER_ASCII8) == 0);
    msx_run_frame(&msx);
    assert(msx.cpu.halted);
    assert(msx.ram[0xc000] == 0x53);
    assert(msx_get_cartridge(&msx, 0)->banks[2] == 3);
    msx_destroy(&msx);
}

static void test_atomic_firmware_set_and_eject(void) {
    static const char *bios_path = "test-firmware-bios.tmp";
    static const char *logo_path = "test-firmware-logo.tmp";
    static const char *subrom_path = "test-firmware-subrom.tmp";
    static const char *disk_path = "test-firmware-disk.tmp";
    static const char *bad_path = "test-firmware-bad.tmp";
    MsxMachine msx;
    u8 old_bios[MSX_BIOS_SIZE];
    u8 bios[MSX_BIOS_SIZE];
    u8 logo[MSX_LOGO_SIZE];
    u8 subrom[MSX_SUBROM_SIZE];
    u8 disk_rom[MSX_DISK_ROM_SIZE];
    u8 cartridge[0x4000];
    u8 bad = 0;

    memset(old_bios, 0x11, sizeof(old_bios));
    memset(bios, 0x22, sizeof(bios));
    memset(logo, 0x2a, sizeof(logo));
    memset(subrom, 0x33, sizeof(subrom));
    memset(disk_rom, 0x44, sizeof(disk_rom));
    memset(cartridge, 0x55, sizeof(cartridge));
    write_fixture(bios_path, bios, sizeof(bios));
    write_fixture(logo_path, logo, sizeof(logo));
    write_fixture(subrom_path, subrom, sizeof(subrom));
    write_fixture(disk_path, disk_rom, sizeof(disk_rom));
    write_fixture(bad_path, &bad, sizeof(bad));

    msx_init(&msx, MSX_MODEL_PHILIPS_NMS8250,
             MSX_REGION_PAL, 128);
    assert(msx_install_bios(&msx, old_bios, sizeof(old_bios)) == 0);
    assert(msx_install_cartridge(&msx, cartridge, sizeof(cartridge)) == 0);
    assert(msx_load_firmware_set(
               &msx, bios_path, NULL, bad_path, disk_path) < 0);
    assert(msx.bios_loaded);
    assert(msx.bios[0] == 0x11);
    assert(!msx.subrom_loaded);
    assert(!msx.disk_rom_loaded);

    assert(msx_load_firmware_set(
               &msx, bios_path, logo_path,
               subrom_path, disk_path) == 0);
    assert(msx.bios_loaded && msx.bios[0] == 0x22);
    assert(msx.logo_loaded && msx.logo[0] == 0x2a);
    assert(msx.subrom_loaded && msx.subrom[0] == 0x33);
    assert(msx.disk_rom_loaded && msx.disk_rom[0] == 0x44);
    assert(msx_get_cartridge(&msx, 0)->loaded);
    msx_eject_firmware(&msx);
    assert(!msx_can_boot(&msx));
    assert(!msx.bios_loaded);
    assert(!msx.logo_loaded);
    assert(!msx.subrom_loaded);
    assert(!msx.disk_rom_loaded);
    assert(msx.bios[0] == 0xff);
    assert(msx_get_cartridge(&msx, 0)->loaded);
    msx_destroy(&msx);

    assert(remove(bios_path) == 0);
    assert(remove(logo_path) == 0);
    assert(remove(subrom_path) == 0);
    assert(remove(disk_path) == 0);
    assert(remove(bad_path) == 0);
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

    msx_init(msx, MSX_MODEL_PHILIPS_NMS8250, MSX_REGION_PAL, 128);
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
    assert(msx_memory_read(msx, 0x7ff7) == disk_rom[0x3ff7]);
    assert(msx_memory_read(msx, 0x7ff8) &
           WD2793_STATUS_NOT_READY);
    assert(msx_memory_read(msx, 0x7fff) == 0xff);
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
    msx_destroy(msx);
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

    /*
     * The VDP IRQ is a level, not a permanently latched CPU edge.
     * Reading S#0 acknowledges it and must cancel a still-pending IRQ;
     * reading another selected status register must leave it asserted.
     */
    msx.vdp.registers[1] |= 0x20;
    vdp_end_frame(&msx.vdp);
    msx.cpu.pending_irq = true;
    msx_io_write(&msx, 0x99, 2);
    msx_io_write(&msx, 0x99, 0x8f);
    (void)msx_io_read(&msx, 0x99);
    assert(msx.vdp.irq);
    assert(msx.cpu.pending_irq);
    msx_io_write(&msx, 0x99, 0);
    msx_io_write(&msx, 0x99, 0x8f);
    assert(msx_io_read(&msx, 0x99) & 0x80);
    assert(!msx.vdp.irq);
    assert(!msx.cpu.pending_irq);

    /* On an MSX1 VDP, 9A/9B mirror the data/control ports. */
    msx_configure(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx.vdp.type == MSX_VDP_TMS9918);
    msx_io_write(&msx, 0x9b, 0x00);
    msx_io_write(&msx, 0x9b, 0x40);
    msx_io_write(&msx, 0x9a, 0xa5);
    assert(msx.vdp.vram[0] == 0xa5);
}

static void test_rtc_ports_and_reset_persistence(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    msx_io_write(&msx, 0xb4, 13);
    msx_io_write(&msx, 0xb5, 2);
    assert(msx_io_read(&msx, 0xb4) == 0xff);
    assert(msx_io_read(&msx, 0xb5) == 0xff);

    msx_configure(&msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_PAL, 128);
    msx_io_write(&msx, 0xb4, 13);
    assert(msx_io_read(&msx, 0xb4) == 0xff);
    assert(msx_io_read(&msx, 0xb5) == 0xf8);

    msx_io_write(&msx, 0xb5, 2); /* CMOS RAM block 2 */
    msx_io_write(&msx, 0xb4, 4);
    msx_io_write(&msx, 0xb5, 0xbe);
    assert(msx_io_read(&msx, 0xb5) == 0xfe);

    msx_reset(&msx);
    msx_io_write(&msx, 0xb4, 13);
    assert(msx_io_read(&msx, 0xb5) == 0xf8);
    msx_io_write(&msx, 0xb5, 2);
    msx_io_write(&msx, 0xb4, 4);
    assert(msx_io_read(&msx, 0xb5) == 0xfe);
}

static void test_rtc_restart_persistence(void) {
    const char *path = "test-msx-rtc.tmp";
    MsxMachine msx;

    (void)remove(path);
    msx_init(&msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_PAL, 128);
    assert(msx_set_rtc_persistence(&msx, path, 1000) == 0);
    assert(msx_rtc_persistence_active(&msx));
    assert(msx_rtc_persistence_dirty(&msx));
    assert(strcmp(msx_rtc_persistence_path(&msx), path) == 0);

    msx_io_write(&msx, 0xb4, 13);
    msx_io_write(&msx, 0xb5, 2);
    msx_io_write(&msx, 0xb4, 4);
    msx_io_write(&msx, 0xb5, 0x0b);
    assert(msx_flush_rtc_persistence(&msx, 1000) == 0);
    assert(!msx_rtc_persistence_dirty(&msx));
    assert(!msx_rtc_persistence_has_error(&msx));
    msx_destroy(&msx);

    msx_init(&msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_PAL, 128);
    assert(msx_set_rtc_persistence(&msx, path, 2000) == 0);
    msx_io_write(&msx, 0xb4, 13);
    msx_io_write(&msx, 0xb5, 2);
    msx_io_write(&msx, 0xb4, 4);
    assert(msx_io_read(&msx, 0xb5) == 0xfb);
    msx_reset(&msx);
    msx_io_write(&msx, 0xb4, 13);
    msx_io_write(&msx, 0xb5, 2);
    msx_io_write(&msx, 0xb4, 4);
    assert(msx_io_read(&msx, 0xb5) == 0xfb);

    msx_io_write(&msx, 0xb4, 5);
    msx_io_write(&msx, 0xb5, 0x0c);
    assert(msx_set_rtc_persistence(&msx, "", 3000) == 0);
    assert(!msx_rtc_persistence_active(&msx));
    assert(msx_set_rtc_persistence(&msx, path, 3000) == 0);
    msx_io_write(&msx, 0xb4, 13);
    msx_io_write(&msx, 0xb5, 2);
    msx_io_write(&msx, 0xb4, 5);
    assert(msx_io_read(&msx, 0xb5) == 0xfc);
    assert(msx_set_rtc_persistence(&msx, "", 3000) == 0);
    msx_destroy(&msx);
    assert(remove(path) == 0);
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

static void test_dual_joystick_psg_ports(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_joystick_read_port(&msx, 0) == MSX_JOY_MASK);
    assert(msx_joystick_read_port(&msx, 1) == MSX_JOY_MASK);
    assert(msx_joystick_read_port(&msx, MSX_JOYSTICK_PORTS) ==
           MSX_JOY_MASK);

    msx_joystick_set_pressed(
        &msx, 0, MSX_JOY_UP | MSX_JOY_RIGHT | MSX_JOY_TRIGGER_A);
    msx_joystick_set_pressed(
        &msx, 1, MSX_JOY_DOWN | MSX_JOY_LEFT | MSX_JOY_TRIGGER_B);
    msx_joystick_set_pressed(&msx, MSX_JOYSTICK_PORTS, MSX_JOY_MASK);
    assert(msx_joystick_read_port(&msx, 0) == 0x26);
    assert(msx_joystick_read_port(&msx, 1) == 0x19);

    /* R15 bit 6 selects port A (0) or B (1). */
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) == 0xa6);
    msx_io_write(&msx, 0xa0, 15);
    msx_io_write(&msx, 0xa1, 0x40);
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) == 0x99);

    /* R15 bits 4 and 5 raise pin 8 on ports A and B respectively. */
    msx_io_write(&msx, 0xa0, 15);
    msx_io_write(&msx, 0xa1, 0x10);
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) == 0xbf);
    msx_io_write(&msx, 0xa0, 15);
    msx_io_write(&msx, 0xa1, 0x60);
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) == 0xbf);

    msx_reset(&msx);
    assert(msx_joystick_read_port(&msx, 0) == MSX_JOY_MASK);
    assert(msx_joystick_read_port(&msx, 1) == MSX_JOY_MASK);
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) == 0xbf);
    msx_destroy(&msx);
}

static void test_cassette_ppi_and_psg_path(void) {
    static const u8 cas[] = {
        0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74,
        0x00
    };
    const u64 first_negative_sample =
        (u64)CASSETTE_SAMPLE_RATE * 2u + 1u;
    MsxMachine msx;
    size_t position_before_reset;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(cassette_mount(
               &msx.cassette, cas, sizeof(cas), msx.cycles) == 0);
    assert(msx_cassette_mounted(&msx));
    assert(!cassette_is_motor_on(&msx.cassette));
    assert(msx.cassette.output);

    /* PSG R14 bit 7 is the cassette comparator; silence rests high. */
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) & 0x80);

    /* PPI C bit 4 is the active-low motor relay; bit 5 is output. */
    msx_io_write(&msx, 0xaa, 0xef);
    assert(cassette_is_motor_on(&msx.cassette));
    assert(msx_cassette_rolling(&msx));
    msx.cycles =
        (first_negative_sample * MSX_CPU_HZ +
         CASSETTE_SAMPLE_RATE - 1u) /
        CASSETTE_SAMPLE_RATE;
    assert(!(msx_io_read(&msx, 0xa2) & 0x80));

    /* The 8255 bit set/reset form controls the same physical lines. */
    msx_io_write(&msx, 0xab, 0x09); /* set bit 4: motor off */
    assert(!cassette_is_motor_on(&msx.cassette));
    msx_io_write(&msx, 0xab, 0x0a); /* reset bit 5: output low */
    assert(!msx.cassette.output);

    position_before_reset = msx.cassette.position;
    msx_reset(&msx);
    assert(msx_cassette_mounted(&msx));
    assert(msx.cassette.position == position_before_reset);
    assert(!cassette_is_motor_on(&msx.cassette));
    assert(msx.cassette.output);

    msx_rewind_cassette(&msx);
    assert(msx_cassette_position_ms(&msx) == 0);
    msx_eject_cassette(&msx);
    assert(!msx_cassette_mounted(&msx));
    msx_io_write(&msx, 0xa0, 14);
    assert(msx_io_read(&msx, 0xa2) & 0x80);
    msx_destroy(&msx);
}

static void test_cassette_audible_monitor_mix(void) {
    static const u8 cas[] = {
        0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74,
        0xea, 0xea, 0xea, 0xea, 0xea,
        0xea, 0xea, 0xea, 0xea, 0xea, 0x1a
    };
    u8 bios[MSX_BIOS_SIZE] = { 0 };
    MsxMachine msx;
    bool heard_tape = false;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_install_bios(&msx, bios, sizeof(bios)) == 0);
    assert(cassette_mount(
               &msx.cassette, cas, sizeof(cas), msx.cycles) == 0);
    msx.cassette.position = CASSETTE_SAMPLE_RATE * 2u;
    cassette_set_motor(&msx.cassette, true, msx.cycles);
    msx_set_cassette_audible_monitor(&msx, true);
    msx_run_frame(&msx);
    for (size_t i = 0; i < msx.audio_sample_count; ++i)
        if (msx.audio_samples[i] != 0)
            heard_tape = true;
    assert(heard_tape);

    msx_set_cassette_audible_monitor(&msx, false);
    msx_run_frame(&msx);
    for (size_t i = 0; i < msx.audio_sample_count; ++i)
        assert(msx.audio_samples[i] == 0);
    msx_destroy(&msx);
}

static void write_psg_register(MsxMachine *msx, u8 reg, u8 value) {
    msx_io_write(msx, 0xa0, reg);
    msx_io_write(msx, 0xa1, value);
}

static u8 read_psg_joystick_port(MsxMachine *msx) {
    msx_io_write(msx, 0xa0, 14);
    return msx_io_read(msx, 0xa2);
}

static void test_msx_mouse_psg_protocol(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(!msx_mouse_enabled(&msx, 0));
    assert(!msx_mouse_enabled(&msx, 1));
    msx_mouse_set_enabled(&msx, 0, true);
    assert(msx_mouse_enabled(&msx, 0));

    /*
     * Host movement is halved and negated like openMSX. The first rising
     * pin-8 strobe latches X=-16 (F0) and Y=8 (08).
     */
    msx_mouse_add_host_motion(&msx, 0, 32, -16);
    msx_mouse_set_button(&msx, 0, 0, true);
    write_psg_register(&msx, 15, 0x10);
    assert(read_psg_joystick_port(&msx) == 0xaf);
    write_psg_register(&msx, 15, 0x00);
    assert(read_psg_joystick_port(&msx) == 0xa0);
    write_psg_register(&msx, 15, 0x10);
    assert(read_psg_joystick_port(&msx) == 0xa0);
    write_psg_register(&msx, 15, 0x00);
    assert(read_psg_joystick_port(&msx) == 0xa8);

    /*
     * The alternate four-nibble cycle is zero for trackball detection,
     * while button state remains live.
     */
    write_psg_register(&msx, 15, 0x10);
    assert(read_psg_joystick_port(&msx) == 0xa0);
    msx_mouse_set_button(&msx, 0, 0, false);
    assert(read_psg_joystick_port(&msx) == 0xb0);

    /*
     * Port B uses R15 bit 5 as pin 8 and bit 6 for selection. Its state
     * and two-button input remain independent from port A.
     */
    msx_mouse_set_enabled(&msx, 1, true);
    msx_mouse_add_host_motion(&msx, 1, -8, 8);
    msx_mouse_set_button(&msx, 1, 1, true);
    write_psg_register(&msx, 15, 0x60);
    assert(read_psg_joystick_port(&msx) == 0x90);
    write_psg_register(&msx, 15, 0x40);
    assert(read_psg_joystick_port(&msx) == 0x94);
    write_psg_register(&msx, 15, 0x60);
    assert(read_psg_joystick_port(&msx) == 0x9f);
    write_psg_register(&msx, 15, 0x40);
    assert(read_psg_joystick_port(&msx) == 0x9c);

    /* A long pause resynchronizes the next rising strobe to X high. */
    msx_mouse_set_button(&msx, 1, 1, false);
    msx_mouse_add_host_motion(&msx, 1, -40, 0);
    msx.cycles += MSX_CPU_HZ;
    write_psg_register(&msx, 15, 0x60);
    assert(read_psg_joystick_port(&msx) == 0xb1);

    /*
     * Every PSG write refreshes the timeout, even when pin 8 remains at
     * the same level. Two sub-timeout pauses therefore continue the
     * current scan instead of forcing another synchronization.
     */
    msx.cycles += MSX_CPU_HZ / 1000;
    write_psg_register(&msx, 15, 0x60);
    msx.cycles += MSX_CPU_HZ / 1000;
    write_psg_register(&msx, 15, 0x40);
    assert(read_psg_joystick_port(&msx) == 0xb4);

    msx_mouse_clear_input(&msx, 1);
    assert(read_psg_joystick_port(&msx) == 0xb0);
    msx_reset(&msx);
    assert(msx_mouse_enabled(&msx, 0));
    assert(msx_mouse_enabled(&msx, 1));
    assert(read_psg_joystick_port(&msx) == 0xb0);
    msx_mouse_set_enabled(&msx, 0, false);
    assert(!msx_mouse_enabled(&msx, 0));
    msx_destroy(&msx);
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
    msx_destroy(msx);
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
    assert(msx_get_cartridge(msx, 0)->loaded);
    assert(msx->vdp.vram[0] == 0xa5);
    assert(msx->cpu.pc >= 0x4010 && msx->cpu.pc < 0x4020);
    msx_destroy(msx);
    free(msx);
}

static void test_msx_diag_bios_checkpoint_if_available(void) {
    const char *diagnostic_path = getenv("MSX_DIAG_BIOS_ROM");
    MsxMachine *msx;
    size_t nonzero_vram = 0;

    if (!diagnostic_path || !diagnostic_path[0])
        return;

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_load_bios(msx, diagnostic_path) == 0);
    for (int frame = 0; frame < 300; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;

    fprintf(stderr,
            "MSX-DIAG BIOS checkpoint: frame=%llu PC=%04X SP=%04X "
            "slot=%02X cycles=%llu instructions=%llu VRAM=%zu "
            "R0=%02X R1=%02X R2=%02X R4=%02X R7=%02X\n",
            (unsigned long long)msx->frame, msx->cpu.pc, msx->cpu.sp,
            msx->primary_slot, (unsigned long long)msx->cycles,
            (unsigned long long)msx->instructions, nonzero_vram,
            msx->vdp.registers[0], msx->vdp.registers[1],
            msx->vdp.registers[2], msx->vdp.registers[4],
            msx->vdp.registers[7]);
    assert(msx->frame == 300);
    assert(msx->bios_loaded);
    assert(msx->instructions > 1000000);
    assert(msx->cpu.pc >= 0x00cb && msx->cpu.pc <= 0x010a);
    assert(msx->primary_slot == 0xc0);
    assert(msx->vdp.type == MSX_VDP_TMS9918);
    assert(msx->vdp.registers[0] == 0x00);
    assert(msx->vdp.registers[1] == 0xd0);
    assert(msx->vdp.registers[2] == 0x02);
    assert(msx->vdp.registers[4] == 0x00);
    assert(msx->vdp.registers[7] == 0xf1);
    assert(nonzero_vram == 667);
    assert(memcmp(&msx->vdp.vram[0x080c],
                  "=== MSX DIAG ===", 16) == 0);
    assert(memcmp(&msx->vdp.vram[0x0851],
                  "[0] Monitor", 11) == 0);
    msx_destroy(msx);
    free(msx);
}

static void test_nms8250_checkpoint_if_available(void) {
    const char *directory = getenv("MSX_NMS8250_DIR");
    const char *diagnostic_path = getenv("MSX_DIAG_ROM");
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
    msx_init(msx, MSX_MODEL_PHILIPS_NMS8250, MSX_REGION_PAL, 128);
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
            "R0=%02X R1=%02X R9=%02X R14=%02X CMD=%02X\n",
            (unsigned long long)msx->frame, msx->cpu.pc, msx->cpu.sp,
            msx->primary_slot, msx->secondary_slot[3],
            msx->mapper_segment[0], msx->mapper_segment[1],
            msx->mapper_segment[2], msx->mapper_segment[3],
            (unsigned long long)msx->cycles,
            (unsigned long long)msx->instructions, nonzero_vram,
            msx->vdp.registers[0], msx->vdp.registers[1],
            msx->vdp.registers[9], msx->vdp.registers[14],
            msx->vdp.registers[46]);
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
    assert(nonzero_vram > 0);
    assert(msx->vdp.registers[0] == 0x08);
    assert(msx->vdp.registers[1] == 0x60);

    if (diagnostic_path && diagnostic_path[0]) {
        assert(msx_load_cartridge(msx, diagnostic_path) == 0);
        for (int frame = 0; frame < 1500; ++frame)
            msx_run_frame(msx);
        fprintf(stderr,
                "MSX2 diagnostic checkpoint: frame=%llu PC=%04X "
                "R0=%02X R1=%02X R2=%02X R7=%02X R15=%02X\n",
                (unsigned long long)msx->frame, msx->cpu.pc,
                msx->vdp.registers[0], msx->vdp.registers[1],
                msx->vdp.registers[2], msx->vdp.registers[7],
                msx->vdp.registers[15]);
        assert(msx->frame == 1500);
        assert(msx->cpu.pc == 0x468c);
        assert(msx->cpu.halted);
        assert(msx->vdp.registers[0] == 0x00);
        assert(msx->vdp.registers[1] == 0x70);
        assert(msx->vdp.registers[2] == 0x00);
        assert(msx->vdp.registers[7] == 0xf4);
        assert(msx->vdp.registers[15] == 0x00);
        assert(memcmp(&msx->vdp.vram[0x29],
                      "MSX DIAGNOSTICS", 15) == 0);
    }
    msx_destroy(msx);
    free(msx);
}

static u64 vdp_frame_hash(const MsxVdp *vdp) {
    const u8 *bytes = (const u8 *)vdp->pixels;
    size_t size =
        (size_t)vdp->render_width * vdp->render_height *
        sizeof(vdp->pixels[0]);
    u64 hash = 1469598103934665603ULL;

    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool bytes_contain(const u8 *data, size_t size,
                          const char *text) {
    size_t length = strlen(text);

    if (length > size)
        return false;
    for (size_t offset = 0; offset <= size - length; ++offset) {
        if (memcmp(data + offset, text, length) == 0)
            return true;
    }
    return false;
}

static void test_nms8250_floppy_checkpoint_if_available(void) {
    const char *directory = getenv("MSX_NMS8250_DIR");
    const char *image_path = getenv("MSX_NMS8250_DSK");
    MsxMachine *msx;
    char bios_path[4096];
    char subrom_path[4096];
    char disk_rom_path[4096];
    size_t nonzero_vram = 0;
    u64 framebuffer_hash;

    if (!directory || !directory[0] ||
        !image_path || !image_path[0])
        return;
    snprintf(bios_path, sizeof(bios_path),
             "%s/nms8250_basic-bios2.rom", directory);
    snprintf(subrom_path, sizeof(subrom_path),
             "%s/nms8250_msx2sub.rom", directory);
    snprintf(disk_rom_path, sizeof(disk_rom_path),
             "%s/nms8250_disk.rom", directory);

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_PHILIPS_NMS8250,
             MSX_REGION_PAL, 128);
    assert(msx_load_firmware_set(
               msx, bios_path, "", subrom_path,
               disk_rom_path) == 0);
    assert(msx_mount_drive_a(
               msx, image_path,
               FLOPPY_IMAGE_READ_ONLY) == 0);
    for (int frame = 0; frame < 3000; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;
    framebuffer_hash = vdp_frame_hash(&msx->vdp);

    fprintf(stderr,
            "NMS 8250 floppy checkpoint: frame=%llu PC=%04X "
            "slot=%02X subslot=%02X track=%u side=%u sector=%u "
            "instructions=%llu VRAM=%zu framebuffer=%016llX\n",
            (unsigned long long)msx->frame, msx->cpu.pc,
            msx->primary_slot, msx->secondary_slot[3],
            msx->fdc.physical_track, msx->fdc.side_reg & 1,
            msx->fdc.sector,
            (unsigned long long)msx->instructions,
            nonzero_vram,
            (unsigned long long)framebuffer_hash);
    assert(msx->frame == 3000);
    assert(msx_drive_a_mounted(msx));
    assert(msx_drive_a_take_activity(msx));
    assert(msx->instructions > 1000000);
    assert(nonzero_vram > 1000);
    assert(framebuffer_hash != 0);
    assert(msx->fdc.drive_a.tracks == 80);
    assert(msx->fdc.drive_a.sides == 2);
    assert(msx->fdc.drive_a.sectors_per_track == 9);
    msx_destroy(msx);
    free(msx);
}

static void set_nextor_fixture_rtc(MsxRtc *rtc) {
    memset(rtc, 0, sizeof(*rtc));
    rtc->registers[0][6] = 6;  /* Saturday */
    rtc->registers[0][7] = 1;  /* 1983-01-01 00:00:00 */
    rtc->registers[0][9] = 1;
    rtc->registers[0][11] = 3;
    rtc->registers[0][12] = 8;
    rtc->registers[1][10] = 1; /* 24-hour mode */
    rtc->registers[1][11] = 3;
    rtc_reset(rtc);
}

static void test_nextor_sunrise_checkpoint_if_available(void) {
    const char *directory = getenv("MSX_NMS8250_DIR");
    const char *sunrise_path = getenv("MSX_NEXTOR_SUNRISE_ROM");
    const char *image_path = getenv("MSX_NEXTOR_IDE_IMAGE");
    MsxMachine *msx;
    char bios_path[4096];
    char subrom_path[4096];
    size_t nonzero_vram = 0;
    u64 framebuffer_hash;

    if (!directory || !directory[0] ||
        !sunrise_path || !sunrise_path[0] ||
        !image_path || !image_path[0])
        return;
    snprintf(bios_path, sizeof(bios_path),
             "%s/nms8250_basic-bios2.rom", directory);
    snprintf(subrom_path, sizeof(subrom_path),
             "%s/nms8250_msx2sub.rom", directory);

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_PHILIPS_NMS8250, MSX_REGION_PAL, 128);
    assert(msx_load_bios(msx, bios_path) == 0);
    assert(msx_load_subrom(msx, subrom_path) == 0);
    /*
     * The NMS 8250 disk ROM expects a WD2793, which is a separate future
     * device. Keep it absent so the external Sunrise kernel owns boot.
     */
    assert(!msx->disk_rom_loaded);
    assert(msx_load_sunrise_ide(msx, 1, sunrise_path) == 0);
    assert(msx_mount_sunrise_disk(msx, image_path) == 0);
    set_nextor_fixture_rtc(&msx->rtc);
    for (int frame = 0; frame < 2001; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;
    framebuffer_hash = vdp_frame_hash(&msx->vdp);

    fprintf(stderr,
            "Nextor/Sunrise checkpoint: frame=%llu PC=%04X SP=%04X "
            "slot=%02X subslot=%02X mapper=%02X,%02X,%02X,%02X "
            "cycles=%llu instructions=%llu VRAM=%zu R0=%02X R1=%02X "
            "framebuffer=%016llX\n",
            (unsigned long long)msx->frame, msx->cpu.pc, msx->cpu.sp,
            msx->primary_slot, msx->secondary_slot[3],
            msx->mapper_segment[0], msx->mapper_segment[1],
            msx->mapper_segment[2], msx->mapper_segment[3],
            (unsigned long long)msx->cycles,
            (unsigned long long)msx->instructions, nonzero_vram,
            msx->vdp.registers[0], msx->vdp.registers[1],
            (unsigned long long)framebuffer_hash);
    assert(msx->frame == 2001);
    assert(msx_sunrise_connected(msx));
    assert(msx_sunrise_disk_mounted(msx));
    /*
     * Advertising ATA FLUSH CACHE changes the deterministic instruction
     * phase at this frame boundary: Nextor is inside the BIOS interrupt
     * path, with page 0 temporarily mapped to slot 0. The exact desktop
     * framebuffer below remains the primary boot checkpoint.
     */
    assert(msx->primary_slot == 0xfc);
    assert(msx->secondary_slot[3] == 0xaa);
    assert(msx->mapper_segment[0] == 3);
    assert(msx->mapper_segment[1] == 2);
    assert(msx->mapper_segment[2] == 1);
    assert(msx->mapper_segment[3] == 0);
    assert(msx->cpu.pc >= 0x0360 && msx->cpu.pc <= 0x0370);
    assert(msx->instructions > 15000000);
    assert(msx->vdp.registers[0] == 0x0a);
    assert(msx->vdp.registers[1] == 0x62);
    assert(nonzero_vram == 8596);
    assert(framebuffer_hash == 0x7fd8af872d7e64f1ULL);
    msx_destroy(msx);
    free(msx);
}

static void test_nextor_sd_mapper_checkpoint_if_available(void) {
    const char *bios_path = getenv("MSX_SD_MAPPER_BIOS_ROM");
    const char *mapper_path = getenv("MSX_SD_MAPPER_ROM");
    const char *image_path = getenv("MSX_SD_MAPPER_IMAGE");
    MsxMachine *msx;
    size_t nonzero_vram = 0;
    u64 framebuffer_hash;

    if (!bios_path || !bios_path[0] ||
        !mapper_path || !mapper_path[0] ||
        !image_path || !image_path[0])
        return;

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_load_bios(msx, bios_path) == 0);
    assert(msx_load_sd_mapper(msx, 1, mapper_path) == 0);
    assert(msx_mount_sd_card(
               msx, 0, image_path, SD_IMAGE_READ_ONLY) == 0);
    for (int frame = 0; frame < 900; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;
    framebuffer_hash = vdp_frame_hash(&msx->vdp);

    fprintf(stderr,
            "Nextor/SD Mapper V2 checkpoint: frame=%llu PC=%04X "
            "slot=%02X cart-subslot=%02X mapper=%02X,%02X,%02X,%02X "
            "instructions=%llu VRAM=%zu framebuffer=%016llX\n",
            (unsigned long long)msx->frame, msx->cpu.pc,
            msx->primary_slot, msx->sd_mapper.secondary_slot,
            msx->sd_mapper.mapper_segment[0],
            msx->sd_mapper.mapper_segment[1],
            msx->sd_mapper.mapper_segment[2],
            msx->sd_mapper.mapper_segment[3],
            (unsigned long long)msx->instructions, nonzero_vram,
            (unsigned long long)framebuffer_hash);
    assert(msx->frame == 900);
    assert(msx_sd_mapper_connected(msx));
    assert(msx_sd_mapper_slot(msx) == 1);
    assert(msx_sd_card_mounted(msx, 0));
    assert(msx_sd_card_take_activity(msx, 0));
    assert(msx->sd_mapper.mapper_enabled);
    assert(msx->instructions > 1000000);
    assert(nonzero_vram > 100);
    assert(framebuffer_hash != 0);
    msx_destroy(msx);
    free(msx);
}

static void test_nextor_megaflash_checkpoint_if_available(void) {
    const char *bios_path = getenv("MSX_MEGAFLASH_BIOS_ROM");
    const char *flash_path = getenv("MSX_MEGAFLASH_ROM");
    const char *image_path = getenv("MSX_MEGAFLASH_IMAGE");
    MsxMachine *msx;
    size_t nonzero_vram = 0;
    u64 framebuffer_hash;

    if (!bios_path || !bios_path[0] ||
        !flash_path || !flash_path[0])
        return;

    msx = malloc(sizeof(*msx));
    assert(msx);
    msx_init(msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(msx_load_bios(msx, bios_path) == 0);
    assert(msx_load_megaflash(msx, 1, flash_path) == 0);
    if (image_path && image_path[0])
        assert(msx_mount_megaflash_card(
                   msx, 0, image_path, SD_IMAGE_READ_ONLY) == 0);
    for (int frame = 0; frame < 1200; ++frame)
        msx_run_frame(msx);
    for (size_t i = 0; i < sizeof(msx->vdp.vram); ++i)
        if (msx->vdp.vram[i])
            ++nonzero_vram;
    framebuffer_hash = vdp_frame_hash(&msx->vdp);

    fprintf(stderr,
            "Nextor/MegaFlashROM SCC+ SD checkpoint: frame=%llu "
            "PC=%04X slot=%02X cart-subslot=%02X "
            "mapper=%02X,%02X,%02X,%02X instructions=%llu "
            "VRAM=%zu framebuffer=%016llX\n",
            (unsigned long long)msx->frame, msx->cpu.pc,
            msx->primary_slot, msx->megaflash.secondary_slot,
            msx->megaflash.mapper_segment[0],
            msx->megaflash.mapper_segment[1],
            msx->megaflash.mapper_segment[2],
            msx->megaflash.mapper_segment[3],
            (unsigned long long)msx->instructions, nonzero_vram,
            (unsigned long long)framebuffer_hash);
    assert(msx->frame == 1200);
    assert(msx_megaflash_connected(msx));
    assert(msx_megaflash_slot(msx) == 1);
    assert(!image_path || !image_path[0] ||
           msx_megaflash_card_mounted(msx, 0));
    assert(msx->instructions > 1000000);
    assert(nonzero_vram > 100);
    assert(framebuffer_hash != 0);
    assert(bytes_contain(
               msx->vdp.vram, sizeof(msx->vdp.vram),
               "NEXTOR.SYS"));
    msx_destroy(msx);
    free(msx);
}

int main(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(strcmp(msx.profile->name, "MSX") == 0);
    assert(strcmp(msx_vdp_name(&msx), "TMS9929A") == 0);
    assert(msx.profile->vram_kb == 16);
    assert(!msx.profile->expanded_slots);
    assert(!msx.profile->memory_mapper);
    assert(!msx_has_memory_mapper(&msx));
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
    assert(msx_has_memory_mapper(&msx));
    assert(msx.profile->rtc);
    assert(msx.frame_hz == 60);
    assert(msx.frame == 0);

    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX1, 64, 1) == 128);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX1, 16, -1) == 4096);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX2, 128, 1) == 256);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX2, 4096, 1) == 64);
    {
        MsxModel detected;
        assert(msx_model_from_name("nms8250", &detected));
        assert(detected == MSX_MODEL_PHILIPS_NMS8250);
        assert(strcmp(msx_model_config_name(detected),
                      "nms8250") == 0);
        assert(msx_model_is_msx2(detected));
        assert(msx_profile(detected)->requires_subrom);
    }

    msx_configure(&msx, MSX_MODEL_GENERIC_MSX1,
                  MSX_REGION_PAL, 4096);
    assert(msx.ram_kb == 4096);
    assert(msx.ram_capacity == MSX_RAM_MAX_SIZE);
    assert(msx.ram != msx.internal_ram);
    assert(msx_has_memory_mapper(&msx));
    msx_io_write(&msx, 0xa8, 0xff);
    msx_io_write(&msx, 0xfc, 0xff);
    assert(msx_io_read(&msx, 0xfc) == 0xff);
    msx_memory_write(&msx, 0x0123, 0x83);
    assert(msx.ram[0x3fc123] == 0x83);
    msx_io_write(&msx, 0xfc, 0xfe);
    assert(msx_memory_read(&msx, 0x0123) == 0);
    msx_io_write(&msx, 0xfc, 0xff);
    assert(msx_memory_read(&msx, 0x0123) == 0x83);

    msx_configure(&msx, MSX_MODEL_PHILIPS_NMS8250,
                  MSX_REGION_PAL, 128);
    assert(strcmp(msx.profile->name, "Philips NMS 8250") == 0);
    assert(msx.vdp.type == MSX_VDP_V9938);
    assert(msx.ram == msx.internal_ram);

    test_slot_bus_and_cpu();
    test_dual_cartridge_slots_and_mapper_reset();
    test_sunrise_cartridge_slot_bus();
    test_sd_mapper_expanded_cartridge_bus();
    test_megaflash_expanded_cartridge_bus();
    test_ascii8_cpu_boot_checkpoint();
    test_atomic_firmware_set_and_eject();
    test_msx2_expanded_slots_and_firmware();
    test_vdp_ports_and_renderer();
    test_msx2_vdp_extended_ports();
    test_rtc_ports_and_reset_persistence();
    test_rtc_restart_persistence();
    test_keyboard_matrix_and_ppi();
    test_dual_joystick_psg_ports();
    test_cassette_ppi_and_psg_path();
    test_cassette_audible_monitor_mix();
    test_msx_mouse_psg_protocol();
    test_psg_ports_and_cycle_timed_audio();
    test_cbios_checkpoint_if_available();
    test_msx_diag_bios_checkpoint_if_available();
    test_nms8250_checkpoint_if_available();
    test_nms8250_floppy_checkpoint_if_available();
    test_nextor_sunrise_checkpoint_if_available();
    test_nextor_sd_mapper_checkpoint_if_available();
    test_nextor_megaflash_checkpoint_if_available();
    msx_destroy(&msx);
    return 0;
}
