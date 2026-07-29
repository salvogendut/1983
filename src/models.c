#include "models.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
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
    add_default(catalog, "msx1", "MSX", MSX_MODEL_GENERIC_MSX1);
    add_default(catalog, "msx2", "MSX2", MSX_MODEL_GENERIC_MSX2);
    add_default(catalog, "nms8250", "Philips NMS 8250",
                MSX_MODEL_PHILIPS_NMS8250);
}

static void default_path(char *path, size_t path_size) {
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home_dir;

    if (file_exists("1983-models.conf")) {
        snprintf(path, path_size, "1983-models.conf");
        return;
    }
    if (xdg_config && xdg_config[0]) {
        snprintf(path, path_size, "%s/1983/1983-models.conf",
                 xdg_config);
        if (file_exists(path))
            return;
    }
    home_dir = getenv("HOME");
    if (home_dir && home_dir[0]) {
        snprintf(path, path_size,
                 "%s/.config/1983/1983-models.conf", home_dir);
        if (file_exists(path))
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
