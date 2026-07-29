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

    int audio_volume;

    bool second_drive;
    bool sunrise_ide;
    bool scc;
    bool msx_music;
    bool kanji_rom;

    bool       tinker;
    bool       debug;
    NotifyMode notifications;

    char bios_path[PATH_MAX];
    char logo_path[PATH_MAX];
    char subrom_path[PATH_MAX];
    char disk_rom_path[PATH_MAX];
    char cartridge_path[MSX_CARTRIDGE_SLOTS][PATH_MAX];
    MsxCartridgeMapper cartridge_mapper[MSX_CARTRIDGE_SLOTS];
    char last_media_dir[PATH_MAX];
    char path[PATH_MAX];
} Config;

void config_defaults(Config *config);
void config_normalize(Config *config);
void config_load(Config *config, const char *path);
int  config_save(const Config *config);
