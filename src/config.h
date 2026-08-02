#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "msx.h"
#include "models.h"
#include "notify.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DISPLAY_CRT_SCANLINES_DEFAULT 35

#define GIF_CAPTURE_WIDTH_DEFAULT 720
#define GIF_CAPTURE_FPS_DEFAULT   25

typedef enum {
    INPUT_PORT_A = 0,
    INPUT_PORT_B
} InputPort;

typedef enum {
    JOY_PORT_JOYSTICK = 0,
    JOY_PORT_MOUSE
} JoyPortDevice;

typedef struct {
    MsxModel  model;
    char      machine_id[MODEL_ID_MAX];
    MsxRegion region;
    int       memory_kb;

    int  scale;
    bool fullscreen;
    bool smoothing;
    bool real_crt;
    int  crt_scanlines;

    int  gif_width;
    int  gif_fps;
    bool gif_ffmpeg;

    int audio_volume;

    InputPort     main_input;
    JoyPortDevice joy_port_device[2];

    bool extra_hardware;
    bool second_drive;
    bool sunrise_ide;
    bool sd_mapper;
    bool megaflash;
    bool tcpip_unapi;
    bool rs232;
    bool sd_mapper_ram;
    bool sd_mapper_alternate_driver;
    FloppyImageMode floppy_image_mode;
    AtaImageMode ide_image_mode;
    SdImageMode sd_image_mode;
    bool scc;
    bool msx_music;
    bool kanji_rom;

    bool       tinker;
    bool       rtc_persistence;
    bool       cassette_audible_monitor;
    bool       cassette_visual_monitor;
    bool       debug;
    NotifyMode notifications;

    char bios_path[PATH_MAX];
    char logo_path[PATH_MAX];
    char subrom_path[PATH_MAX];
    char disk_rom_path[PATH_MAX];
    char sunrise_rom_path[PATH_MAX];
    char sd_mapper_rom_path[PATH_MAX];
    char rs232_rom_path[PATH_MAX];
    char sd_card_path[MSX_SD_MAPPER_CARDS][PATH_MAX];
    char megaflash_rom_path[PATH_MAX];
    char megaflash_card_path[MSX_MEGAFLASH_CARDS][PATH_MAX];
    char drive_a_path[PATH_MAX];
    char drive_b_path[PATH_MAX];
    char ide_image_path[PATH_MAX];
    char cassette_path[PATH_MAX];
    char cartridge_path[MSX_CARTRIDGE_SLOTS][PATH_MAX];
    MsxCartridgeMapper cartridge_mapper[MSX_CARTRIDGE_SLOTS];
    char last_media_dir[PATH_MAX];
    char path[PATH_MAX];
} Config;

void config_defaults(Config *config);
void config_normalize(Config *config);
void config_load(Config *config, const char *path);
int  config_save(const Config *config);
int  config_rtc_path(const Config *config, char *path, size_t path_size);
int  config_megaflash_state_path(const Config *config,
                                 char *path, size_t path_size);
int  config_megaflash_pending_state_path(const Config *config,
                                         char *path, size_t path_size);
unsigned config_cartridge_extension_count(const Config *config);
const char *config_cartridge_slot_owner(const Config *config,
                                        unsigned slot);
bool config_cartridge_slot_available(const Config *config,
                                     unsigned slot);
