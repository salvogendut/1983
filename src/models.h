#pragma once

#include <stddef.h>

#include "msx.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MODEL_CATALOG_MAX 64
#define MODEL_ID_MAX 64
#define MODEL_NAME_MAX 96

typedef struct {
    char id[MODEL_ID_MAX];
    char name[MODEL_NAME_MAX];
    MsxModel hardware;
    char bios_path[PATH_MAX];
    char logo_path[PATH_MAX];
    char subrom_path[PATH_MAX];
    char disk_rom_path[PATH_MAX];
    MsxFloppyConfig floppy;
} ModelDefinition;

typedef struct {
    ModelDefinition entries[MODEL_CATALOG_MAX];
    size_t count;
    char path[PATH_MAX];
    char edit_path[PATH_MAX];
} ModelCatalog;

void model_catalog_defaults(ModelCatalog *catalog);
int model_catalog_load(ModelCatalog *catalog, const char *path);
int model_catalog_save(const ModelCatalog *catalog, const char *path);
void model_catalog_user_path(char *path, size_t path_size);
bool model_definition_validate(const ModelCatalog *catalog,
                               const ModelDefinition *definition,
                               size_t replaced_index,
                               bool check_firmware,
                               char *error, size_t error_size);
const ModelDefinition *model_catalog_find(const ModelCatalog *catalog,
                                          const char *id);
const ModelDefinition *model_catalog_find_hardware(
    const ModelCatalog *catalog, MsxModel hardware);
size_t model_catalog_index(const ModelCatalog *catalog,
                           const char *id);
