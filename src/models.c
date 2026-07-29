#include "models.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define GETCWD(buffer, size) _getcwd((buffer), (int)(size))
#define MKDIR(path) _mkdir(path)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define GETCWD(buffer, size) getcwd((buffer), (size))
#define MKDIR(path) mkdir((path), 0755)
#endif

#ifndef PKGDATADIR
#define PKGDATADIR ""
#endif

static char *trim(char *text) {
    char *end;

    while (isspace((unsigned char)*text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return text;
}

static bool file_exists(const char *path) {
    FILE *file;

    if (!path || !path[0])
        return false;
    file = fopen(path, "r");
    if (!file)
        return false;
    fclose(file);
    return true;
}

static bool path_is_absolute(const char *path) {
    if (!path || !path[0])
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

static void ensure_parent(const char *path) {
    char copy[PATH_MAX];
    char *cursor;

    snprintf(copy, sizeof(copy), "%s", path ? path : "");
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

static void path_dirname(char *destination, size_t destination_size,
                         const char *path) {
    char *slash;
    char *backslash;

    snprintf(destination, destination_size, "%s", path ? path : "");
    slash = strrchr(destination, '/');
    backslash = strrchr(destination, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
    if (!slash) {
        snprintf(destination, destination_size, ".");
    } else if (slash == destination) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
}

static void resolve_path(char *destination, size_t destination_size,
                         const char *directory, const char *path) {
    if (!path || !path[0]) {
        destination[0] = '\0';
    } else if (path_is_absolute(path) ||
               !directory || !directory[0] ||
               strcmp(directory, ".") == 0) {
        snprintf(destination, destination_size, "%s", path);
    } else {
        snprintf(destination, destination_size, "%s/%s",
                 directory, path);
    }
}

void model_catalog_user_path(char *path, size_t path_size) {
#ifdef _WIN32
    const char *base = getenv("APPDATA");

    if (!base || !base[0])
        base = getenv("LOCALAPPDATA");
    if (!base || !base[0])
        base = getenv("USERPROFILE");
    if (base && base[0]) {
        snprintf(path, path_size, "%s/1983/1983-models.conf", base);
        return;
    }
#else
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home_dir = getenv("HOME");

    if (xdg_config && xdg_config[0]) {
        snprintf(path, path_size, "%s/1983/1983-models.conf",
                 xdg_config);
        return;
    }
    if (home_dir && home_dir[0]) {
        snprintf(path, path_size,
                 "%s/.config/1983/1983-models.conf", home_dir);
        return;
    }
#endif
    snprintf(path, path_size, "1983-models.conf");
}

static void add_default(ModelCatalog *catalog, const char *id,
                        const char *name, MsxModel hardware) {
    ModelDefinition *definition;

    if (catalog->count >= MODEL_CATALOG_MAX)
        return;
    definition = &catalog->entries[catalog->count++];
    memset(definition, 0, sizeof(*definition));
    snprintf(definition->id, sizeof(definition->id), "%s", id);
    snprintf(definition->name, sizeof(definition->name), "%s", name);
    definition->hardware = hardware;
}

void model_catalog_defaults(ModelCatalog *catalog) {
    if (!catalog)
        return;
    memset(catalog, 0, sizeof(*catalog));
    model_catalog_user_path(catalog->edit_path,
                            sizeof(catalog->edit_path));
    add_default(catalog, "msx1", "MSX", MSX_MODEL_GENERIC_MSX1);
    add_default(catalog, "msx2", "MSX2", MSX_MODEL_GENERIC_MSX2);
    add_default(catalog, "nms8250", "Philips NMS 8250",
                MSX_MODEL_PHILIPS_NMS8250);
}

static void default_path(char *path, size_t path_size) {
    char user_path[PATH_MAX];

    model_catalog_user_path(user_path, sizeof(user_path));
    if (file_exists(user_path)) {
        snprintf(path, path_size, "%s", user_path);
        return;
    }
    if (file_exists("1983-models.conf")) {
        snprintf(path, path_size, "1983-models.conf");
        return;
    }
    if (PKGDATADIR[0]) {
        snprintf(path, path_size, "%s/1983-models.conf", PKGDATADIR);
        if (file_exists(path))
            return;
    }
    path[0] = '\0';
}

static ModelDefinition *begin_definition(ModelCatalog *catalog,
                                         char *header) {
    char *id = header;
    ModelDefinition *definition;

    if (strncasecmp(header, "model ", 6) == 0)
        id = trim(header + 6);
    if (!id[0] || strlen(id) >= MODEL_ID_MAX ||
        catalog->count >= MODEL_CATALOG_MAX)
        return NULL;
    definition = &catalog->entries[catalog->count++];
    memset(definition, 0, sizeof(*definition));
    snprintf(definition->id, sizeof(definition->id), "%s", id);
    definition->hardware = MSX_MODEL_COUNT;
    return definition;
}

static void set_definition_value(ModelDefinition *definition,
                                 const char *directory,
                                 const char *key, const char *value) {
    MsxModel hardware;

    if (!definition)
        return;
    if (strcasecmp(key, "name") == 0) {
        snprintf(definition->name, sizeof(definition->name), "%s", value);
    } else if (strcasecmp(key, "hardware") == 0) {
        if (msx_model_from_name(value, &hardware))
            definition->hardware = hardware;
    } else if (strcasecmp(key, "bios") == 0) {
        resolve_path(definition->bios_path,
                     sizeof(definition->bios_path), directory, value);
    } else if (strcasecmp(key, "logo") == 0) {
        resolve_path(definition->logo_path,
                     sizeof(definition->logo_path), directory, value);
    } else if (strcasecmp(key, "subrom") == 0) {
        resolve_path(definition->subrom_path,
                     sizeof(definition->subrom_path), directory, value);
    } else if (strcasecmp(key, "disk_rom") == 0) {
        resolve_path(definition->disk_rom_path,
                     sizeof(definition->disk_rom_path), directory, value);
    }
}

static void compact_catalog(ModelCatalog *catalog) {
    size_t destination = 0;

    for (size_t source = 0; source < catalog->count; ++source) {
        ModelDefinition *definition = &catalog->entries[source];
        bool duplicate = false;

        if (definition->hardware == MSX_MODEL_COUNT)
            continue;
        if (!definition->name[0])
            snprintf(definition->name, sizeof(definition->name),
                     "%s", definition->id);
        for (size_t i = 0; i < destination; ++i) {
            if (strcasecmp(catalog->entries[i].id,
                           definition->id) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (destination != source)
            catalog->entries[destination] = *definition;
        ++destination;
    }
    catalog->count = destination;
}

int model_catalog_load(ModelCatalog *catalog, const char *path) {
    char selected_path[PATH_MAX];
    char directory[PATH_MAX];
    char line[PATH_MAX + 128];
    ModelDefinition *current = NULL;
    FILE *file;

    if (!catalog)
        return -1;
    memset(catalog, 0, sizeof(*catalog));
    model_catalog_user_path(catalog->edit_path,
                            sizeof(catalog->edit_path));
    if (path && path[0])
        snprintf(selected_path, sizeof(selected_path), "%s", path);
    else
        default_path(selected_path, sizeof(selected_path));
    if (!selected_path[0]) {
        model_catalog_defaults(catalog);
        return -1;
    }
    file = fopen(selected_path, "r");
    if (!file) {
        model_catalog_defaults(catalog);
        return -1;
    }
    snprintf(catalog->path, sizeof(catalog->path), "%s", selected_path);
    path_dirname(directory, sizeof(directory), selected_path);

    while (fgets(line, sizeof(line), file)) {
        char *text = trim(line);
        char *separator;
        char *end;

        if (!text[0] || text[0] == '#' || text[0] == ';')
            continue;
        if (text[0] == '[') {
            end = strrchr(text, ']');
            if (!end)
                continue;
            *end = '\0';
            current = begin_definition(catalog, trim(text + 1));
            continue;
        }
        separator = strchr(text, '=');
        if (!separator)
            continue;
        *separator++ = '\0';
        set_definition_value(current, directory,
                             trim(text), trim(separator));
    }
    fclose(file);
    compact_catalog(catalog);
    if (!catalog->count) {
        model_catalog_defaults(catalog);
        return -1;
    }
    return 0;
}

static bool valid_id(const char *id) {
    if (!id || !id[0] || strlen(id) >= MODEL_ID_MAX)
        return false;
    for (; *id; ++id) {
        unsigned char character = (unsigned char)*id;
        if (!isalnum(character) &&
            character != '-' && character != '_' && character != '.')
            return false;
    }
    return true;
}

static bool has_line_break(const char *text) {
    return text && strpbrk(text, "\r\n") != NULL;
}

static bool file_has_size(const char *path, long expected_size) {
    FILE *file;
    long size;

    if (!path || !path[0])
        return true;
    file = fopen(path, "rb");
    if (!file)
        return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    size = ftell(file);
    fclose(file);
    return size == expected_size;
}

static bool validation_error(char *error, size_t error_size,
                             const char *message) {
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
    return false;
}

bool model_definition_validate(const ModelCatalog *catalog,
                               const ModelDefinition *definition,
                               size_t replaced_index,
                               bool check_firmware,
                               char *error, size_t error_size) {
    if (error && error_size)
        error[0] = '\0';
    if (!catalog || !definition)
        return validation_error(error, error_size,
                                "No machine definition");
    if (!valid_id(definition->id))
        return validation_error(
            error, error_size,
            "ID must use letters, numbers, '.', '_' or '-'");
    if (!definition->name[0])
        return validation_error(error, error_size,
                                "Display name cannot be empty");
    if (has_line_break(definition->name))
        return validation_error(error, error_size,
                                "Display name cannot contain line breaks");
    if (has_line_break(definition->bios_path) ||
        has_line_break(definition->logo_path) ||
        has_line_break(definition->subrom_path) ||
        has_line_break(definition->disk_rom_path))
        return validation_error(error, error_size,
                                "Firmware paths cannot contain line breaks");
    if ((unsigned)definition->hardware >= MSX_MODEL_COUNT)
        return validation_error(error, error_size,
                                "Unsupported hardware layout");
    for (size_t i = 0; i < catalog->count; ++i) {
        if (i != replaced_index &&
            strcasecmp(catalog->entries[i].id,
                       definition->id) == 0)
            return validation_error(error, error_size,
                                    "Machine ID is already in use");
    }
    if (!check_firmware)
        return true;
    if (!file_has_size(definition->bios_path, MSX_BIOS_SIZE))
        return validation_error(
            error, error_size,
            "BIOS must be empty or exactly 32 KB");
    if (!file_has_size(definition->logo_path, MSX_LOGO_SIZE))
        return validation_error(
            error, error_size,
            "Logo ROM must be empty or exactly 16 KB");
    if (!file_has_size(definition->subrom_path, MSX_SUBROM_SIZE))
        return validation_error(
            error, error_size,
            "Sub-ROM must be empty or exactly 16 KB");
    if (!file_has_size(definition->disk_rom_path,
                       MSX_DISK_ROM_SIZE))
        return validation_error(
            error, error_size,
            "Disk ROM must be empty or exactly 16 KB");
    return true;
}

static void save_path(const char *path, char *saved, size_t saved_size) {
    char absolute_directory[PATH_MAX];

    if (!path || !path[0] || path_is_absolute(path)) {
        snprintf(saved, saved_size, "%s", path ? path : "");
        return;
    }
    if (!GETCWD(absolute_directory, sizeof(absolute_directory))) {
        snprintf(saved, saved_size, "%s", path);
        return;
    }
    resolve_path(saved, saved_size, absolute_directory, path);
}

int model_catalog_save(const ModelCatalog *catalog, const char *path) {
    char temporary[PATH_MAX];
    FILE *file;

    if (!catalog || !path || !path[0] || !catalog->count)
        return -1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
        (int)sizeof(temporary))
        return -1;
    ensure_parent(path);
    file = fopen(temporary, "w");
    if (!file) {
        perror("model_catalog_save");
        return -1;
    }
    fputs("# 1983 machine catalogue\n"
          "# Managed by Advanced > Machine model editor.\n"
          "# Empty firmware fields are selected when the model is used.\n\n",
          file);
    for (size_t i = 0; i < catalog->count; ++i) {
        const ModelDefinition *definition = &catalog->entries[i];
        char bios[PATH_MAX];
        char logo[PATH_MAX];
        char subrom[PATH_MAX];
        char disk_rom[PATH_MAX];

        if (!model_definition_validate(
                catalog, definition, i, false, NULL, 0)) {
            fclose(file);
            remove(temporary);
            return -1;
        }
        save_path(definition->bios_path, bios, sizeof(bios));
        save_path(definition->logo_path, logo, sizeof(logo));
        save_path(definition->subrom_path, subrom, sizeof(subrom));
        save_path(definition->disk_rom_path,
                  disk_rom, sizeof(disk_rom));
        fprintf(file,
                "[model %s]\n"
                "name = %s\n"
                "hardware = %s\n"
                "bios = %s\n"
                "logo = %s\n"
                "subrom = %s\n"
                "disk_rom = %s\n\n",
                definition->id, definition->name,
                msx_model_config_name(definition->hardware),
                bios, logo, subrom, disk_rom);
    }
    if (fclose(file) != 0) {
        remove(temporary);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING |
                     MOVEFILE_WRITE_THROUGH)) {
        remove(temporary);
        errno = EIO;
        return -1;
    }
#else
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return -1;
    }
#endif
    return 0;
}

const ModelDefinition *model_catalog_find(const ModelCatalog *catalog,
                                          const char *id) {
    if (!catalog || !id)
        return NULL;
    for (size_t i = 0; i < catalog->count; ++i) {
        if (strcasecmp(catalog->entries[i].id, id) == 0)
            return &catalog->entries[i];
    }
    return NULL;
}

const ModelDefinition *model_catalog_find_hardware(
    const ModelCatalog *catalog, MsxModel hardware) {
    if (!catalog)
        return NULL;
    for (size_t i = 0; i < catalog->count; ++i) {
        if (catalog->entries[i].hardware == hardware)
            return &catalog->entries[i];
    }
    return NULL;
}

size_t model_catalog_index(const ModelCatalog *catalog,
                           const char *id) {
    if (!catalog || !id)
        return 0;
    for (size_t i = 0; i < catalog->count; ++i) {
        if (strcasecmp(catalog->entries[i].id, id) == 0)
            return i;
    }
    return 0;
}
