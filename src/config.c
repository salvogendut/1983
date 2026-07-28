#include "config.h"

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
    if (!value)
        return fallback;
    if (strcasecmp(value, "msx1") == 0 ||
        strcasecmp(value, "generic-msx1") == 0)
        return MSX_MODEL_GENERIC_MSX1;
    if (strcasecmp(value, "msx2") == 0 ||
        strcasecmp(value, "generic-msx2") == 0)
        return MSX_MODEL_GENERIC_MSX2;
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
    config->region = MSX_REGION_PAL;
    config->memory_kb = msx_default_ram_kb(config->model);
    config->scale = 1;
    config->smoothing = false;
    config->crt_scanlines = DISPLAY_CRT_SCANLINES_DEFAULT;
    config->notifications = NOTIFY_MODE_SCREEN;
}

void config_normalize(Config *config) {
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
    if ((unsigned)config->notifications > NOTIFY_MODE_CONSOLE)
        config->notifications = NOTIFY_MODE_SCREEN;
}

void config_load(Config *config, const char *path) {
    char line[512];
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

        if (strcmp(key, "model") == 0)
            config->model = parse_model(value, config->model);
        else if (strcmp(key, "region") == 0)
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
        else if (strcmp(key, "second_drive") == 0)
            config->second_drive = parse_bool(value, config->second_drive);
        else if (strcmp(key, "sunrise_ide") == 0)
            config->sunrise_ide = parse_bool(value, config->sunrise_ide);
        else if (strcmp(key, "scc") == 0)
            config->scc = parse_bool(value, config->scc);
        else if (strcmp(key, "msx_music") == 0)
            config->msx_music = parse_bool(value, config->msx_music);
        else if (strcmp(key, "kanji_rom") == 0)
            config->kanji_rom = parse_bool(value, config->kanji_rom);
        else if (strcmp(key, "tinker") == 0)
            config->tinker = parse_bool(value, config->tinker);
        else if (strcmp(key, "debug") == 0)
            config->debug = parse_bool(value, config->debug);
        else if (strcmp(key, "notifications") == 0)
            config->notifications =
                parse_notifications(value, config->notifications);
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
            config->model == MSX_MODEL_GENERIC_MSX2 ? "msx2" : "msx1");
    fprintf(file, "region = %s\n",
            config->region == MSX_REGION_NTSC ? "ntsc" : "pal");
    fprintf(file, "memory_kb = %d\n\n", config->memory_kb);
    fprintf(file, "[display]\n");
    fprintf(file, "scale = %d\n", config->scale);
    fprintf(file, "fullscreen = %s\n", bool_name(config->fullscreen));
    fprintf(file, "smoothing = %s\n", bool_name(config->smoothing));
    fprintf(file, "real_crt = %s\n", bool_name(config->real_crt));
    fprintf(file, "crt_scanlines = %d\n\n", config->crt_scanlines);
    fprintf(file, "[extensions]\n");
    fprintf(file, "second_drive = %s\n", bool_name(config->second_drive));
    fprintf(file, "sunrise_ide = %s\n", bool_name(config->sunrise_ide));
    fprintf(file, "scc = %s\n", bool_name(config->scc));
    fprintf(file, "msx_music = %s\n", bool_name(config->msx_music));
    fprintf(file, "kanji_rom = %s\n\n", bool_name(config->kanji_rom));
    fprintf(file, "[advanced]\n");
    fprintf(file, "tinker = %s\n", bool_name(config->tinker));
    fprintf(file, "debug = %s\n", bool_name(config->debug));
    fprintf(file, "notifications = %s\n",
            config->notifications == NOTIFY_MODE_SCREEN ? "screen" :
            config->notifications == NOTIFY_MODE_CONSOLE ? "console" : "off");

    return fclose(file) == 0 ? 0 : -1;
}
