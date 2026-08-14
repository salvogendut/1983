#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define strcasecmp _stricmp
#else
#include <strings.h>
#include <sys/stat.h>
#define MKDIR(path) mkdir((path), 0755)
#endif

static bool parse_bool(const char *value, bool fallback) {
    if (!value)
        return fallback;
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0)
        return true;
    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0)
        return false;
    return fallback;
}

static const char *bool_name(bool value) {
    return value ? "true" : "false";
}

static MsxModel parse_model(const char *value, MsxModel fallback) {
    MsxModel model;

    if (msx_model_from_name(value, &model))
        return model;
    return fallback;
}

static MsxRegion parse_region(const char *value, MsxRegion fallback) {
    if (!value)
        return fallback;
    if (strcasecmp(value, "pal") == 0)
        return MSX_REGION_PAL;
    if (strcasecmp(value, "ntsc") == 0)
        return MSX_REGION_NTSC;
    return fallback;
}

static NotifyMode parse_notifications(const char *value,
                                      NotifyMode fallback) {
    if (!value)
        return fallback;
    if (strcasecmp(value, "screen") == 0)
        return NOTIFY_MODE_SCREEN;
    if (strcasecmp(value, "console") == 0)
        return NOTIFY_MODE_CONSOLE;
    if (strcasecmp(value, "off") == 0)
        return NOTIFY_MODE_OFF;
    return fallback;
}

static InputPort parse_input_port(const char *value,
                                  InputPort fallback) {
    if (!value)
        return fallback;
    if (strcasecmp(value, "joy_port_a") == 0 ||
        strcasecmp(value, "a") == 0)
        return INPUT_PORT_A;
    if (strcasecmp(value, "joy_port_b") == 0 ||
        strcasecmp(value, "b") == 0)
        return INPUT_PORT_B;
    return fallback;
}

static JoyPortDevice parse_joy_port_device(
    const char *value, JoyPortDevice fallback) {
    if (!value)
        return fallback;
    if (strcasecmp(value, "joystick") == 0)
        return JOY_PORT_JOYSTICK;
    if (strcasecmp(value, "mouse") == 0)
        return JOY_PORT_MOUSE;
    return fallback;
}

static void default_path(char *out, size_t out_size) {
#ifdef _WIN32
    const char *base = getenv("APPDATA");
    if (!base || !base[0])
        base = getenv("LOCALAPPDATA");
    if (!base || !base[0])
        base = getenv("USERPROFILE");
    if (base && base[0]) {
        snprintf(out, out_size, "%s/1983/1983.conf", base);
        return;
    }
#endif
    const char *home_dir = getenv("HOME");
    if (!home_dir || !home_dir[0])
        home_dir = ".";
    snprintf(out, out_size, "%s/.config/1983/1983.conf", home_dir);
}

static void ensure_parent(const char *path) {
    char copy[PATH_MAX];
    char *cursor;

    snprintf(copy, sizeof(copy), "%s", path);
    cursor = strrchr(copy, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(copy, '\\');
        if (!cursor || (backslash && backslash > cursor))
            cursor = backslash;
    }
#endif
    if (!cursor)
        return;
    *cursor = '\0';

    cursor = copy;
    if (*cursor == '/')
        ++cursor;
#ifdef _WIN32
    if (cursor[0] && cursor[1] == ':')
        cursor += 2;
#endif
    while ((cursor = strpbrk(cursor, "/\\")) != NULL) {
        char saved = *cursor;
        *cursor = '\0';
        if (copy[0])
            MKDIR(copy);
        *cursor = saved;
        ++cursor;
    }
    if (copy[0])
        MKDIR(copy);
}

void config_defaults(Config *config) {
    memset(config, 0, sizeof(*config));
    config->model = MSX_MODEL_GENERIC_MSX1;
    snprintf(config->machine_id, sizeof(config->machine_id), "cbios");
    config->region = MSX_REGION_PAL;
    config->memory_kb = msx_default_ram_kb(config->model);
    config->scale = 1;
    config->smoothing = false;
    config->crt_scanlines = DISPLAY_CRT_SCANLINES_DEFAULT;
    config->gif_width = GIF_CAPTURE_WIDTH_DEFAULT;
    config->gif_fps = GIF_CAPTURE_FPS_DEFAULT;
    config->gif_ffmpeg = false;
    config->audio_volume = 80;
    config->main_input = INPUT_PORT_A;
    config->joy_port_device[0] = JOY_PORT_JOYSTICK;
    config->joy_port_device[1] = JOY_PORT_JOYSTICK;
    config->notifications = NOTIFY_MODE_SCREEN;
    config->rtc_persistence = true;
    config->floppy_image_mode = FLOPPY_IMAGE_READ_ONLY;
    config->floppy.controller = MSX_FLOPPY_CONTROLLER_NONE;
    config->floppy.primary_slot = -1;
    config->floppy.secondary_slot = -1;
    config->ide_image_mode = ATA_IMAGE_READ_ONLY;
    config->sd_image_mode = SD_IMAGE_READ_ONLY;
    config->sd_mapper_ram = true;
}

void config_normalize(Config *config) {
    bool *cartridge_extensions[] = {
        &config->sunrise_ide,
        &config->sd_mapper,
        &config->megaflash,
        &config->scc,
        &config->msx_music,
        &config->rs232,
        &config->cdx2,
        &config->rdf600,
    };
    unsigned connected = 0;
    unsigned cartridge_capacity = MSX_CARTRIDGE_SLOTS;

    if ((unsigned)config->model >= MSX_MODEL_COUNT)
        config->model = MSX_MODEL_GENERIC_MSX1;
    if (config->region != MSX_REGION_NTSC)
        config->region = MSX_REGION_PAL;
    config->memory_kb =
        msx_normalize_ram_kb(config->model, config->memory_kb);
    if (config->scale < 1)
        config->scale = 1;
    if (config->scale > 4)
        config->scale = 4;
    if (config->crt_scanlines < 0)
        config->crt_scanlines = 0;
    if (config->crt_scanlines > 95)
        config->crt_scanlines = 95;
    if (config->audio_volume < 0)
        config->audio_volume = 0;
    if (config->audio_volume > 100)
        config->audio_volume = 100;
    if (config->ide_image_mode != ATA_IMAGE_READ_WRITE)
        config->ide_image_mode = ATA_IMAGE_READ_ONLY;
    if (config->sd_image_mode != SD_IMAGE_READ_WRITE)
        config->sd_image_mode = SD_IMAGE_READ_ONLY;
    if (config->floppy_image_mode != FLOPPY_IMAGE_READ_WRITE)
        config->floppy_image_mode = FLOPPY_IMAGE_READ_ONLY;
    if (config->cdx2_rom_bank > 1)
        config->cdx2_rom_bank = 0;
    if (!msx_floppy_config_valid(config->model, &config->floppy)) {
        config->floppy.controller = MSX_FLOPPY_CONTROLLER_NONE;
        config->floppy.primary_slot = -1;
        config->floppy.secondary_slot = -1;
    } else if (config->floppy.controller ==
                   MSX_FLOPPY_CONTROLLER_NONE) {
        config->floppy.primary_slot = -1;
        config->floppy.secondary_slot = -1;
    } else if (config->floppy.primary_slot == 1 ||
               config->floppy.primary_slot == 2) {
        --cartridge_capacity;
    }
    if (config->main_input != INPUT_PORT_B)
        config->main_input = INPUT_PORT_A;
    for (unsigned port = 0; port < 2; ++port) {
        if (config->joy_port_device[port] != JOY_PORT_MOUSE)
            config->joy_port_device[port] = JOY_PORT_JOYSTICK;
    }
    if ((unsigned)config->notifications > NOTIFY_MODE_CONSOLE)
        config->notifications = NOTIFY_MODE_SCREEN;
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        if ((unsigned)config->cartridge_mapper[slot] >=
            MSX_CART_MAPPER_COUNT)
            config->cartridge_mapper[slot] = MSX_CART_MAPPER_AUTO;
    }
    for (size_t i = 0;
         i < sizeof(cartridge_extensions) /
             sizeof(cartridge_extensions[0]);
         ++i) {
        if (!*cartridge_extensions[i])
            continue;
        if (connected >= cartridge_capacity)
            *cartridge_extensions[i] = false;
        else
            ++connected;
    }
}

void config_load(Config *config, const char *path) {
    char line[PATH_MAX + 64];
    FILE *file;

    config_defaults(config);
    if (path)
        snprintf(config->path, sizeof(config->path), "%s", path);
    else
        default_path(config->path, sizeof(config->path));

    file = fopen(config->path, "r");
    if (!file)
        return;

    while (fgets(line, sizeof(line), file)) {
        char *key = line;
        char *value;
        char *end;

        while (*key == ' ' || *key == '\t')
            ++key;
        if (!*key || *key == '#' || *key == ';' || *key == '[' ||
            *key == '\r' || *key == '\n')
            continue;
        value = strchr(key, '=');
        if (!value)
            continue;
        *value++ = '\0';
        end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        while (*value == ' ' || *value == '\t')
            ++value;
        end = value + strlen(value);
        while (end > value &&
               (end[-1] == ' ' || end[-1] == '\t' ||
                end[-1] == '\r' || end[-1] == '\n'))
            *--end = '\0';

        if (strcmp(key, "model") == 0) {
            snprintf(config->machine_id,
                     sizeof(config->machine_id), "%s", value);
            config->model = parse_model(value, config->model);
        } else if (strcmp(key, "hardware") == 0) {
            config->model = parse_model(value, config->model);
        } else if (strcmp(key, "region") == 0)
            config->region = parse_region(value, config->region);
        else if (strcmp(key, "memory_kb") == 0)
            config->memory_kb = atoi(value);
        else if (strcmp(key, "scale") == 0)
            config->scale = atoi(value);
        else if (strcmp(key, "fullscreen") == 0)
            config->fullscreen = parse_bool(value, config->fullscreen);
        else if (strcmp(key, "smoothing") == 0)
            config->smoothing = parse_bool(value, config->smoothing);
        else if (strcmp(key, "real_crt") == 0)
            config->real_crt = parse_bool(value, config->real_crt);
        else if (strcmp(key, "crt_scanlines") == 0)
            config->crt_scanlines = atoi(value);
        else if (strcmp(key, "gif_resolution") == 0) {
            int w, h;
            if (sscanf(value, "%dx%d", &w, &h) == 2)
                config->gif_width = w;
        } else if (strcmp(key, "gif_fps") == 0)
            config->gif_fps = atoi(value);
        else if (strcmp(key, "gif_ffmpeg") == 0)
            config->gif_ffmpeg = parse_bool(value, config->gif_ffmpeg);
        else if (strcmp(key, "audio_volume") == 0)
            config->audio_volume = atoi(value);
        else if (strcmp(key, "cassette_audible_monitor") == 0)
            config->cassette_audible_monitor =
                parse_bool(value, config->cassette_audible_monitor);
        else if (strcmp(key, "cassette_visual_monitor") == 0)
            config->cassette_visual_monitor =
                parse_bool(value, config->cassette_visual_monitor);
        else if (strcmp(key, "main_input") == 0)
            config->main_input =
                parse_input_port(value, config->main_input);
        else if (strcmp(key, "joy_port_a") == 0)
            config->joy_port_device[0] =
                parse_joy_port_device(
                    value, config->joy_port_device[0]);
        else if (strcmp(key, "joy_port_b") == 0)
            config->joy_port_device[1] =
                parse_joy_port_device(
                    value, config->joy_port_device[1]);
        else if (strcmp(key, "extra_hardware") == 0)
            config->extra_hardware =
                parse_bool(value, config->extra_hardware);
        else if (strcmp(key, "second_drive") == 0)
            config->second_drive = parse_bool(value, config->second_drive);
        else if (strcmp(key, "sunrise_ide") == 0)
            config->sunrise_ide = parse_bool(value, config->sunrise_ide);
        else if (strcmp(key, "sd_mapper") == 0)
            config->sd_mapper = parse_bool(value, config->sd_mapper);
        else if (strcmp(key, "megaflash") == 0)
            config->megaflash = parse_bool(value, config->megaflash);
        else if (strcmp(key, "tcpip_unapi") == 0)
            config->tcpip_unapi =
                parse_bool(value, config->tcpip_unapi);
        else if (strcmp(key, "rs232") == 0)
            config->rs232 = parse_bool(value, config->rs232);
        else if (strcmp(key, "cdx2") == 0)
            config->cdx2 = parse_bool(value, config->cdx2);
        else if (strcmp(key, "cdx2_rom_bank") == 0)
            config->cdx2_rom_bank = (unsigned)atoi(value);
        else if (strcmp(key, "rdf600") == 0)
            config->rdf600 = parse_bool(value, config->rdf600);
        else if (strcmp(key, "sd_mapper_ram") == 0)
            config->sd_mapper_ram =
                parse_bool(value, config->sd_mapper_ram);
        else if (strcmp(key, "sd_mapper_alternate_driver") == 0)
            config->sd_mapper_alternate_driver =
                parse_bool(value,
                           config->sd_mapper_alternate_driver);
        else if (strcmp(key, "scc") == 0)
            config->scc = parse_bool(value, config->scc);
        else if (strcmp(key, "msx_music") == 0)
            config->msx_music = parse_bool(value, config->msx_music);
        else if (strcmp(key, "kanji_rom") == 0)
            config->kanji_rom = parse_bool(value, config->kanji_rom);
        else if (strcmp(key, "tinker") == 0)
            config->tinker = parse_bool(value, config->tinker);
        else if (strcmp(key, "rtc_persistence") == 0)
            config->rtc_persistence =
                parse_bool(value, config->rtc_persistence);
        else if (strcmp(key, "debug") == 0)
            config->debug = parse_bool(value, config->debug);
        else if (strcmp(key, "notifications") == 0)
            config->notifications =
                parse_notifications(value, config->notifications);
        else if (strcmp(key, "bios") == 0)
            snprintf(config->bios_path,
                     sizeof(config->bios_path), "%s", value);
        else if (strcmp(key, "logo") == 0)
            snprintf(config->logo_path,
                     sizeof(config->logo_path), "%s", value);
        else if (strcmp(key, "subrom") == 0)
            snprintf(config->subrom_path,
                     sizeof(config->subrom_path), "%s", value);
        else if (strcmp(key, "disk_rom") == 0)
            snprintf(config->disk_rom_path,
                     sizeof(config->disk_rom_path), "%s", value);
        else if (strcmp(key, "sunrise_rom") == 0)
            snprintf(config->sunrise_rom_path,
                     sizeof(config->sunrise_rom_path), "%s", value);
        else if (strcmp(key, "sd_mapper_rom") == 0)
            snprintf(config->sd_mapper_rom_path,
                     sizeof(config->sd_mapper_rom_path), "%s", value);
        else if (strcmp(key, "rs232_rom") == 0)
            snprintf(config->rs232_rom_path,
                     sizeof(config->rs232_rom_path), "%s", value);
        else if (strcmp(key, "cdx2_rom") == 0)
            snprintf(config->cdx2_rom_path,
                     sizeof(config->cdx2_rom_path), "%s", value);
        else if (strcmp(key, "rdf600_rom") == 0)
            snprintf(config->rdf600_rom_path,
                     sizeof(config->rdf600_rom_path), "%s", value);
        else if (strcmp(key, "sd_card_a") == 0)
            snprintf(config->sd_card_path[0],
                     sizeof(config->sd_card_path[0]), "%s", value);
        else if (strcmp(key, "sd_card_b") == 0)
            snprintf(config->sd_card_path[1],
                     sizeof(config->sd_card_path[1]), "%s", value);
        else if (strcmp(key, "megaflash_rom") == 0)
            snprintf(config->megaflash_rom_path,
                     sizeof(config->megaflash_rom_path), "%s", value);
        else if (strcmp(key, "megaflash_sd_a") == 0)
            snprintf(config->megaflash_card_path[0],
                     sizeof(config->megaflash_card_path[0]),
                     "%s", value);
        else if (strcmp(key, "megaflash_sd_b") == 0)
            snprintf(config->megaflash_card_path[1],
                     sizeof(config->megaflash_card_path[1]),
                     "%s", value);
        else if (strcmp(key, "ide_image") == 0)
            snprintf(config->ide_image_path,
                     sizeof(config->ide_image_path), "%s", value);
        else if (strcmp(key, "drive_a") == 0)
            snprintf(config->drive_a_path,
                     sizeof(config->drive_a_path), "%s", value);
        else if (strcmp(key, "drive_b") == 0)
            snprintf(config->drive_b_path,
                     sizeof(config->drive_b_path), "%s", value);
        else if (strcmp(key, "floppy_image_mode") == 0)
            config->floppy_image_mode =
                strcmp(value, "read-write") == 0 ||
                strcmp(value, "rw") == 0
                ? FLOPPY_IMAGE_READ_WRITE :
                  FLOPPY_IMAGE_READ_ONLY;
        else if (strcmp(key, "ide_image_mode") == 0)
            config->ide_image_mode =
                strcmp(value, "read-write") == 0 ||
                strcmp(value, "rw") == 0
                ? ATA_IMAGE_READ_WRITE : ATA_IMAGE_READ_ONLY;
        else if (strcmp(key, "sd_image_mode") == 0)
            config->sd_image_mode =
                strcmp(value, "read-write") == 0 ||
                strcmp(value, "rw") == 0
                ? SD_IMAGE_READ_WRITE : SD_IMAGE_READ_ONLY;
        else if (strcmp(key, "cassette") == 0)
            snprintf(config->cassette_path,
                     sizeof(config->cassette_path), "%s", value);
        else if (strcmp(key, "cartridge1") == 0)
            snprintf(config->cartridge_path[0],
                     sizeof(config->cartridge_path[0]), "%s", value);
        else if (strcmp(key, "cartridge2") == 0)
            snprintf(config->cartridge_path[1],
                     sizeof(config->cartridge_path[1]), "%s", value);
        else if (strcmp(key, "cartridge1_mapper") == 0) {
            MsxCartridgeMapper mapper;
            if (msx_cartridge_mapper_from_name(value, &mapper))
                config->cartridge_mapper[0] = mapper;
        } else if (strcmp(key, "cartridge2_mapper") == 0) {
            MsxCartridgeMapper mapper;
            if (msx_cartridge_mapper_from_name(value, &mapper))
                config->cartridge_mapper[1] = mapper;
        } else if (strcmp(key, "last_media_dir") == 0)
            snprintf(config->last_media_dir,
                     sizeof(config->last_media_dir), "%s", value);
    }
    fclose(file);
    config_normalize(config);
}

int config_save(const Config *config) {
    FILE *file;

    ensure_parent(config->path);
    file = fopen(config->path, "w");
    if (!file) {
        perror("config_save");
        return -1;
    }

    fprintf(file, "# 1983 - generic MSX / MSX2 emulator\n");
    fprintf(file, "# Edited automatically by the F9 overlay.\n\n");
    fprintf(file, "[machine]\n");
    fprintf(file, "model = %s\n",
            config->machine_id[0]
            ? config->machine_id : msx_model_config_name(config->model));
    fprintf(file, "hardware = %s\n",
            msx_model_config_name(config->model));
    fprintf(file, "region = %s\n",
            config->region == MSX_REGION_NTSC ? "ntsc" : "pal");
    fprintf(file, "memory_kb = %d\n\n", config->memory_kb);
    fprintf(file, "[firmware]\n");
    fprintf(file, "bios = %s\n", config->bios_path);
    fprintf(file, "logo = %s\n", config->logo_path);
    fprintf(file, "subrom = %s\n", config->subrom_path);
    fprintf(file, "disk_rom = %s\n\n", config->disk_rom_path);
    fprintf(file, "[display]\n");
    fprintf(file, "scale = %d\n", config->scale);
    fprintf(file, "fullscreen = %s\n", bool_name(config->fullscreen));
    fprintf(file, "smoothing = %s\n", bool_name(config->smoothing));
    fprintf(file, "real_crt = %s\n", bool_name(config->real_crt));
    fprintf(file, "crt_scanlines = %d\n"
            "gif_resolution = %dx%d\n"
            "gif_fps = %d\n"
            "gif_ffmpeg = %s\n\n",
            config->crt_scanlines,
            config->gif_width, (config->gif_width * 3) / 4,
            config->gif_fps, bool_name(config->gif_ffmpeg));
    fprintf(file, "[audio]\n");
    fprintf(file, "audio_volume = %d\n\n", config->audio_volume);
    fprintf(file, "[input]\n");
    fprintf(file, "main_input = %s\n",
            config->main_input == INPUT_PORT_B
            ? "joy_port_b" : "joy_port_a");
    fprintf(file, "joy_port_a = %s\n",
            config->joy_port_device[0] == JOY_PORT_MOUSE
            ? "mouse" : "joystick");
    fprintf(file, "joy_port_b = %s\n\n",
            config->joy_port_device[1] == JOY_PORT_MOUSE
            ? "mouse" : "joystick");
    fprintf(file, "[media]\n");
    fprintf(file, "cartridge1 = %s\n", config->cartridge_path[0]);
    fprintf(file, "cartridge1_mapper = %s\n",
            msx_cartridge_mapper_name(config->cartridge_mapper[0]));
    fprintf(file, "cartridge2 = %s\n", config->cartridge_path[1]);
    fprintf(file, "cartridge2_mapper = %s\n",
            msx_cartridge_mapper_name(config->cartridge_mapper[1]));
    fprintf(file, "cassette = %s\n", config->cassette_path);
    fprintf(file, "drive_a = %s\n", config->drive_a_path);
    fprintf(file, "drive_b = %s\n", config->drive_b_path);
    fprintf(file, "floppy_image_mode = %s\n",
            config->floppy_image_mode == FLOPPY_IMAGE_READ_WRITE
            ? "read-write" : "read-only");
    fprintf(file, "ide_image = %s\n", config->ide_image_path);
    fprintf(file, "ide_image_mode = %s\n",
            config->ide_image_mode == ATA_IMAGE_READ_WRITE
            ? "read-write" : "read-only");
    fprintf(file, "sd_card_a = %s\n", config->sd_card_path[0]);
    fprintf(file, "sd_card_b = %s\n", config->sd_card_path[1]);
    fprintf(file, "megaflash_sd_a = %s\n",
            config->megaflash_card_path[0]);
    fprintf(file, "megaflash_sd_b = %s\n",
            config->megaflash_card_path[1]);
    fprintf(file, "sd_image_mode = %s\n",
            config->sd_image_mode == SD_IMAGE_READ_WRITE
            ? "read-write" : "read-only");
    fprintf(file, "last_media_dir = %s\n\n", config->last_media_dir);
    fprintf(file, "[extensions]\n");
    fprintf(file, "extra_hardware = %s\n",
            bool_name(config->extra_hardware));
    fprintf(file, "second_drive = %s\n",
            bool_name(config->second_drive));
    fprintf(file, "sunrise_ide = %s\n", bool_name(config->sunrise_ide));
    fprintf(file, "sunrise_rom = %s\n", config->sunrise_rom_path);
    fprintf(file, "sd_mapper = %s\n", bool_name(config->sd_mapper));
    fprintf(file, "sd_mapper_rom = %s\n",
            config->sd_mapper_rom_path);
    fprintf(file, "rs232_rom = %s\n", config->rs232_rom_path);
    fprintf(file, "cdx2_rom = %s\n", config->cdx2_rom_path);
    fprintf(file, "cdx2_rom_bank = %u\n", config->cdx2_rom_bank);
    fprintf(file, "rdf600_rom = %s\n", config->rdf600_rom_path);
    fprintf(file, "sd_mapper_ram = %s\n",
            bool_name(config->sd_mapper_ram));
    fprintf(file, "sd_mapper_alternate_driver = %s\n",
            bool_name(config->sd_mapper_alternate_driver));
    fprintf(file, "megaflash = %s\n",
            bool_name(config->megaflash));
    fprintf(file, "tcpip_unapi = %s\n",
            bool_name(config->tcpip_unapi));
    fprintf(file, "rs232 = %s\n",
            bool_name(config->rs232));
    fprintf(file, "cdx2 = %s\n",
            bool_name(config->cdx2));
    fprintf(file, "rdf600 = %s\n",
            bool_name(config->rdf600));
    fprintf(file, "megaflash_rom = %s\n",
            config->megaflash_rom_path);
    fprintf(file, "scc = %s\n", bool_name(config->scc));
    fprintf(file, "msx_music = %s\n", bool_name(config->msx_music));
    fprintf(file, "kanji_rom = %s\n\n", bool_name(config->kanji_rom));
    fprintf(file, "[advanced]\n");
    fprintf(file, "tinker = %s\n", bool_name(config->tinker));
    fprintf(file, "rtc_persistence = %s\n",
            bool_name(config->rtc_persistence));
    fprintf(file, "cassette_audible_monitor = %s\n",
            bool_name(config->cassette_audible_monitor));
    fprintf(file, "cassette_visual_monitor = %s\n",
            bool_name(config->cassette_visual_monitor));
    fprintf(file, "debug = %s\n", bool_name(config->debug));
    fprintf(file, "notifications = %s\n",
            config->notifications == NOTIFY_MODE_SCREEN ? "screen" :
            config->notifications == NOTIFY_MODE_CONSOLE ? "console" : "off");

    return fclose(file) == 0 ? 0 : -1;
}

int config_rtc_path(const Config *config, char *path, size_t path_size) {
    char directory[PATH_MAX];
    char machine[MODEL_ID_MAX];
    char *separator;
    size_t length;

    if (!config || !path || !path_size)
        return -1;
    path[0] = '\0';
    if (!config->rtc_persistence || !config->path[0] ||
        !msx_model_is_msx2(config->model))
        return 0;
#ifndef _WIN32
    if (strcmp(config->path, "/dev/null") == 0)
        return 0;
#else
    if (strcasecmp(config->path, "NUL") == 0)
        return 0;
#endif
    snprintf(directory, sizeof(directory), "%s", config->path);
    separator = strrchr(directory, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(directory, '\\');
        if (!separator || (backslash && backslash > separator))
            separator = backslash;
    }
#endif
    if (separator == directory) {
        separator[1] = '\0';
    } else if (separator) {
        *separator = '\0';
    } else {
        snprintf(directory, sizeof(directory), ".");
    }
    snprintf(machine, sizeof(machine), "%s",
             config->machine_id[0]
             ? config->machine_id
             : msx_model_config_name(config->model));
    for (size_t i = 0; machine[i]; ++i) {
        unsigned char character = (unsigned char)machine[i];

        if (!isalnum(character) && character != '-' && character != '_')
            machine[i] = '_';
    }
    length = strlen(directory);
    if (snprintf(path, path_size, "%s%srtc/%s.cmos",
                 directory,
                 length && directory[length - 1] != '/' &&
                 directory[length - 1] != '\\' ? "/" : "",
                 machine) >= (int)path_size) {
        path[0] = '\0';
        return -1;
    }
    return 0;
}

int config_megaflash_state_path(const Config *config,
                                char *path, size_t path_size) {
    char directory[PATH_MAX];
    char *separator;
    size_t length;

    if (!config || !path || !path_size)
        return -1;
    path[0] = '\0';
    if (!config->megaflash || !config->path[0])
        return 0;
#ifndef _WIN32
    if (strcmp(config->path, "/dev/null") == 0)
        return 0;
#else
    if (strcasecmp(config->path, "NUL") == 0)
        return 0;
#endif
    snprintf(directory, sizeof(directory), "%s", config->path);
    separator = strrchr(directory, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(directory, '\\');
        if (!separator || (backslash && backslash > separator))
            separator = backslash;
    }
#endif
    if (separator == directory)
        separator[1] = '\0';
    else if (separator)
        *separator = '\0';
    else
        snprintf(directory, sizeof(directory), ".");
    length = strlen(directory);
    if (snprintf(path, path_size,
                 "%s%sflash/megaflashrom-scc-plus-sd.flash",
                 directory,
                 length && directory[length - 1] != '/' &&
                 directory[length - 1] != '\\' ? "/" : "") >=
            (int)path_size) {
        path[0] = '\0';
        return -1;
    }
    return 0;
}

int config_megaflash_pending_state_path(const Config *config,
                                        char *path, size_t path_size) {
    Config state_config;
    char state_path[PATH_MAX];
    unsigned long long hash = 14695981039346656037ull;

    if (!config || !path || !path_size)
        return -1;
    path[0] = '\0';
    if (!config->megaflash_rom_path[0])
        return 0;
    state_config = *config;
    state_config.megaflash = true;
    if (config_megaflash_state_path(
            &state_config, state_path, sizeof(state_path)) != 0)
        return -1;
    if (!state_path[0])
        return 0;
    for (const unsigned char *cursor =
             (const unsigned char *)config->megaflash_rom_path;
         *cursor; ++cursor) {
        hash ^= *cursor;
        hash *= 1099511628211ull;
    }
    if (snprintf(path, path_size, "%s.pending-%016llx",
                 state_path, hash) >= (int)path_size) {
        path[0] = '\0';
        return -1;
    }
    return 0;
}

unsigned config_cartridge_extension_count(const Config *config) {
    if (!config)
        return 0;
    return (config->floppy.controller != MSX_FLOPPY_CONTROLLER_NONE &&
            (config->floppy.primary_slot == 1 ||
             config->floppy.primary_slot == 2) ? 1u : 0u) +
           (config->sunrise_ide ? 1u : 0u) +
           (config->sd_mapper ? 1u : 0u) +
           (config->megaflash ? 1u : 0u) +
           (config->scc ? 1u : 0u) +
           (config->msx_music ? 1u : 0u) +
           (config->rs232 ? 1u : 0u) +
           (config->cdx2 ? 1u : 0u) +
           (config->rdf600 ? 1u : 0u);
}

const char *config_cartridge_slot_owner(const Config *config,
                                        unsigned slot) {
    const char *owners[MSX_CARTRIDGE_SLOTS] = { NULL, NULL };
    const char *extensions[MSX_CARTRIDGE_SLOTS];
    unsigned extension_count = 0;

    if (!config || slot >= MSX_CARTRIDGE_SLOTS)
        return NULL;
    if (config->floppy.controller != MSX_FLOPPY_CONTROLLER_NONE &&
        (config->floppy.primary_slot == 1 ||
         config->floppy.primary_slot == 2))
        owners[config->floppy.primary_slot - 1] = "Floppy controller";
    if (config->sunrise_ide && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "Sunrise IDE";
    if (config->sd_mapper && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "SD Mapper V2";
    if (config->megaflash && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "MegaFlashROM SCC+ SD";
    if (config->scc && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "Konami SCC";
    if (config->msx_music && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "MSX-MUSIC";
    if (config->rs232 && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "RS-232C";
    if (config->cdx2 && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "CDX-2 FDC";
    if (config->rdf600 && extension_count < MSX_CARTRIDGE_SLOTS)
        extensions[extension_count++] = "RDF600 FDC";

    /*
     * Keep cartridge 1 available for ordinary software until a second
     * extension is connected. A controller mapped into primary slot 1 or 2
     * reserves that exact physical port first.
     */
    for (unsigned extension = 0;
         extension < extension_count; ++extension) {
        unsigned destination = owners[1] == NULL ? 1u : 0u;

        if (owners[destination] == NULL)
            owners[destination] = extensions[extension];
    }
    return owners[slot];
}

bool config_cartridge_slot_available(const Config *config,
                                     unsigned slot) {
    return slot < MSX_CARTRIDGE_SLOTS &&
           config_cartridge_slot_owner(config, slot) == NULL;
}
