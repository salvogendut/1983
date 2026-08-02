#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *path = "test-config-roundtrip.tmp";
    Config config;
    Config loaded;
    char rtc_path[PATH_MAX];
    char flash_path[PATH_MAX];

    config_defaults(&config);
    assert(strcmp(config.machine_id, "cbios") == 0);
    assert(!config.bios_path[0]);
    assert(!config.logo_path[0]);
    assert(!config.subrom_path[0]);
    assert(!config.disk_rom_path[0]);
    assert(!config.sunrise_rom_path[0]);
    assert(!config.sd_mapper);
    assert(!config.sd_mapper_rom_path[0]);
    assert(!config.megaflash);
    assert(!config.tcpip_unapi);
    assert(!config.megaflash_rom_path[0]);
    assert(!config.megaflash_card_path[0][0]);
    assert(!config.megaflash_card_path[1][0]);
    assert(!config.sd_card_path[0][0]);
    assert(!config.sd_card_path[1][0]);
    assert(config.sd_mapper_ram);
    assert(!config.sd_mapper_alternate_driver);
    assert(config.sd_image_mode == SD_IMAGE_READ_ONLY);
    assert(!config.drive_a_path[0]);
    assert(!config.drive_b_path[0]);
    assert(config.floppy_image_mode == FLOPPY_IMAGE_READ_ONLY);
    assert(!config.ide_image_path[0]);
    assert(config.ide_image_mode == ATA_IMAGE_READ_ONLY);
    assert(!config.cassette_path[0]);
    assert(!config.cartridge_path[0][0]);
    assert(!config.cartridge_path[1][0]);
    assert(config.cartridge_mapper[0] == MSX_CART_MAPPER_AUTO);
    assert(config.cartridge_mapper[1] == MSX_CART_MAPPER_AUTO);
    assert(config.main_input == INPUT_PORT_A);
    assert(config.joy_port_device[0] == JOY_PORT_JOYSTICK);
    assert(config.joy_port_device[1] == JOY_PORT_JOYSTICK);
    assert(!config.extra_hardware);
    assert(config.rtc_persistence);
    assert(!config.cassette_audible_monitor);
    assert(!config.cassette_visual_monitor);
    assert(config.gif_width == GIF_CAPTURE_WIDTH_DEFAULT);
    assert(config.gif_fps == GIF_CAPTURE_FPS_DEFAULT);
    assert(!config.gif_ffmpeg);
    assert(config_cartridge_extension_count(&config) == 0);
    assert(config_cartridge_slot_available(&config, 0));
    assert(config_cartridge_slot_available(&config, 1));

    snprintf(config.path, sizeof(config.path), "%s", path);
    config.model = MSX_MODEL_PHILIPS_NMS8250;
    snprintf(config.machine_id, sizeof(config.machine_id),
             "my-nms8250");
    assert(config_rtc_path(
               &config, rtc_path, sizeof(rtc_path)) == 0);
    assert(strcmp(rtc_path, "./rtc/my-nms8250.cmos") == 0);
    config.megaflash = true;
    assert(config_megaflash_state_path(
               &config, flash_path, sizeof(flash_path)) == 0);
    assert(strcmp(
               flash_path,
               "./flash/megaflashrom-scc-plus-sd.flash") == 0);
    config.megaflash = false;
    snprintf(config.machine_id, sizeof(config.machine_id),
             "my nms/8250");
    assert(config_rtc_path(
               &config, rtc_path, sizeof(rtc_path)) == 0);
    assert(strcmp(rtc_path, "./rtc/my_nms_8250.cmos") == 0);
    snprintf(config.machine_id, sizeof(config.machine_id),
             "my-nms8250");
    config.rtc_persistence = false;
    assert(config_rtc_path(
               &config, rtc_path, sizeof(rtc_path)) == 0);
    assert(!rtc_path[0]);
    config.memory_kb = 4096;
    snprintf(config.bios_path, sizeof(config.bios_path),
             "/roms/nms8250_basic-bios2.rom");
    snprintf(config.logo_path, sizeof(config.logo_path),
             "/roms/cbios_logo.rom");
    snprintf(config.subrom_path, sizeof(config.subrom_path),
             "/roms/nms8250_msx2sub.rom");
    snprintf(config.disk_rom_path, sizeof(config.disk_rom_path),
             "/roms/nms8250_disk.rom");
    snprintf(config.cartridge_path[0],
             sizeof(config.cartridge_path[0]),
             "/roms/Metal Gear.rom");
    snprintf(config.cartridge_path[1],
             sizeof(config.cartridge_path[1]),
             "/roms/Arkanoid.rom");
    config.cartridge_mapper[0] = MSX_CART_MAPPER_KONAMI_SCC;
    config.cartridge_mapper[1] = MSX_CART_MAPPER_ASCII8;
    config.main_input = INPUT_PORT_B;
    config.joy_port_device[0] = JOY_PORT_MOUSE;
    config.extra_hardware = true;
    config.sunrise_ide = true;
    snprintf(config.sunrise_rom_path,
             sizeof(config.sunrise_rom_path),
             "/roms/Nextor-2.1.1.SunriseIDE.ROM");
    snprintf(config.ide_image_path,
             sizeof(config.ide_image_path),
             "/disks/GBMSX.IMG");
    config.ide_image_mode = ATA_IMAGE_READ_WRITE;
    config.second_drive = true;
    snprintf(config.drive_a_path,
             sizeof(config.drive_a_path),
             "/disks/game-a.dsk");
    snprintf(config.drive_b_path,
             sizeof(config.drive_b_path),
             "/disks/game-b.dsk");
    config.floppy_image_mode = FLOPPY_IMAGE_READ_WRITE;
    snprintf(config.cassette_path,
             sizeof(config.cassette_path),
             "/tapes/software.cas");
    config.sd_mapper = true;
    config.tcpip_unapi = true;
    snprintf(config.sd_mapper_rom_path,
             sizeof(config.sd_mapper_rom_path),
             "/roms/SDXC110.ROM");
    snprintf(config.sd_card_path[0],
             sizeof(config.sd_card_path[0]),
             "/disks/NEXTOR-A.IMG");
    snprintf(config.sd_card_path[1],
             sizeof(config.sd_card_path[1]),
             "/disks/NEXTOR-B.IMG");
    snprintf(config.megaflash_rom_path,
             sizeof(config.megaflash_rom_path),
             "/roms/megaflashrom-scc-plus-sd.rom");
    snprintf(config.megaflash_card_path[0],
             sizeof(config.megaflash_card_path[0]),
             "/disks/MEGA-A.IMG");
    snprintf(config.megaflash_card_path[1],
             sizeof(config.megaflash_card_path[1]),
             "/disks/MEGA-B.IMG");
    config.sd_image_mode = SD_IMAGE_READ_WRITE;
    config.sd_mapper_ram = false;
    config.sd_mapper_alternate_driver = true;
    config.tinker = true;
    config.cassette_audible_monitor = true;
    config.cassette_visual_monitor = true;
    config.gif_width = 360;
    config.gif_fps = 10;
    config.gif_ffmpeg = true;
    assert(config_cartridge_extension_count(&config) == 2);
    assert(strcmp(config_cartridge_slot_owner(&config, 0),
                  "SD Mapper V2") == 0);
    assert(strcmp(config_cartridge_slot_owner(&config, 1),
                  "Sunrise IDE") == 0);
    assert(!config_cartridge_slot_available(&config, 0));
    assert(!config_cartridge_slot_available(&config, 1));
    snprintf(config.last_media_dir, sizeof(config.last_media_dir),
             "/roms");
    assert(config_save(&config) == 0);

    config_load(&loaded, path);
    assert(loaded.model == MSX_MODEL_PHILIPS_NMS8250);
    assert(strcmp(loaded.machine_id, "my-nms8250") == 0);
    assert(loaded.memory_kb == 4096);
    assert(loaded.extra_hardware);
    assert(loaded.sunrise_ide);
    assert(strcmp(loaded.sunrise_rom_path,
                  "/roms/Nextor-2.1.1.SunriseIDE.ROM") == 0);
    assert(strcmp(loaded.ide_image_path,
                  "/disks/GBMSX.IMG") == 0);
    assert(loaded.ide_image_mode == ATA_IMAGE_READ_WRITE);
    assert(loaded.second_drive);
    assert(!loaded.rtc_persistence);
    assert(strcmp(loaded.drive_a_path,
                  "/disks/game-a.dsk") == 0);
    assert(strcmp(loaded.drive_b_path,
                  "/disks/game-b.dsk") == 0);
    assert(loaded.floppy_image_mode ==
           FLOPPY_IMAGE_READ_WRITE);
    assert(strcmp(loaded.cassette_path,
                  "/tapes/software.cas") == 0);
    assert(loaded.sd_mapper);
    assert(loaded.tcpip_unapi);
    assert(strcmp(loaded.sd_mapper_rom_path,
                  "/roms/SDXC110.ROM") == 0);
    assert(strcmp(loaded.sd_card_path[0],
                  "/disks/NEXTOR-A.IMG") == 0);
    assert(strcmp(loaded.sd_card_path[1],
                  "/disks/NEXTOR-B.IMG") == 0);
    assert(!loaded.megaflash);
    assert(strcmp(
               loaded.megaflash_rom_path,
               "/roms/megaflashrom-scc-plus-sd.rom") == 0);
    assert(strcmp(loaded.megaflash_card_path[0],
                  "/disks/MEGA-A.IMG") == 0);
    assert(strcmp(loaded.megaflash_card_path[1],
                  "/disks/MEGA-B.IMG") == 0);
    assert(loaded.sd_image_mode == SD_IMAGE_READ_WRITE);
    assert(!loaded.sd_mapper_ram);
    assert(loaded.sd_mapper_alternate_driver);
    assert(loaded.tinker);
    assert(loaded.cassette_audible_monitor);
    assert(loaded.cassette_visual_monitor);
    assert(loaded.gif_width == 360);
    assert(loaded.gif_fps == 10);
    assert(loaded.gif_ffmpeg);
    assert(strcmp(loaded.bios_path,
                  "/roms/nms8250_basic-bios2.rom") == 0);
    assert(strcmp(loaded.logo_path, "/roms/cbios_logo.rom") == 0);
    assert(strcmp(loaded.subrom_path,
                  "/roms/nms8250_msx2sub.rom") == 0);
    assert(strcmp(loaded.disk_rom_path,
                  "/roms/nms8250_disk.rom") == 0);
    assert(strcmp(loaded.cartridge_path[0],
                  "/roms/Metal Gear.rom") == 0);
    assert(strcmp(loaded.cartridge_path[1],
                  "/roms/Arkanoid.rom") == 0);
    assert(loaded.cartridge_mapper[0] ==
           MSX_CART_MAPPER_KONAMI_SCC);
    assert(loaded.cartridge_mapper[1] ==
           MSX_CART_MAPPER_ASCII8);
    assert(loaded.main_input == INPUT_PORT_B);
    assert(loaded.joy_port_device[0] == JOY_PORT_MOUSE);
    assert(loaded.joy_port_device[1] == JOY_PORT_JOYSTICK);
    assert(strcmp(loaded.last_media_dir, "/roms") == 0);

    loaded.scc = true;
    config_normalize(&loaded);
    assert(loaded.sunrise_ide);
    assert(loaded.sd_mapper);
    assert(!loaded.scc);
    loaded.sunrise_ide = false;
    loaded.megaflash = true;
    config_normalize(&loaded);
    assert(strcmp(config_cartridge_slot_owner(&loaded, 0),
                  "MegaFlashROM SCC+ SD") == 0);
    assert(strcmp(config_cartridge_slot_owner(&loaded, 1),
                  "SD Mapper V2") == 0);
    loaded.main_input = (InputPort)99;
    loaded.joy_port_device[0] = (JoyPortDevice)99;
    config_normalize(&loaded);
    assert(loaded.main_input == INPUT_PORT_A);
    assert(loaded.joy_port_device[0] == JOY_PORT_JOYSTICK);
    assert(remove(path) == 0);

    puts("configuration media tests passed");
    return 0;
}
