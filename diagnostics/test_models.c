#include "models.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char *path = "tests/test-models.tmp";
    static const char *saved_path = "tests/test-models-saved.tmp";
    static const char *firmware_path = "tests/test-model-firmware.tmp";
    ModelCatalog catalog;
    ModelCatalog saved;
    ModelDefinition edited;
    const ModelDefinition *model;
    char error[160];
    FILE *file;

    model_catalog_defaults(&catalog);
    assert(catalog.count == 4);
    assert(catalog.edit_path[0]);
    assert(strcmp(catalog.entries[0].id, "cbios") == 0);
    assert(strstr(catalog.entries[0].bios_path,
                  "ROMS/cbios_main_msx1.rom"));
    assert(strstr(catalog.entries[0].logo_path,
                  "ROMS/cbios_logo_msx1.rom"));
    assert(strcmp(catalog.entries[1].id, "msx1") == 0);
    assert(catalog.entries[3].hardware ==
           MSX_MODEL_PHILIPS_NMS8250);

    file = fopen(path, "w");
    assert(file);
    fputs("[model custom-msx]\n"
          "name = My custom MSX\n"
          "hardware = msx1\n"
          "bios = firmware/main.rom\n"
          "logo = firmware/logo.rom\n"
          "\n"
          "[model custom-msx2]\n"
          "name = My custom MSX2\n"
          "hardware = msx2\n"
          "bios = /firmware/msx2.rom\n"
          "subrom = firmware/sub.rom\n"
          "disk_rom = firmware/disk.rom\n"
          "\n"
          "[model unsupported]\n"
          "name = Not selectable\n"
          "hardware = msx3\n",
          file);
    assert(fclose(file) == 0);

    assert(model_catalog_load(&catalog, path) == 0);
    assert(catalog.count == 2);
    model = model_catalog_find(&catalog, "CUSTOM-MSX");
    assert(model);
    assert(strcmp(model->name, "My custom MSX") == 0);
    assert(model->hardware == MSX_MODEL_GENERIC_MSX1);
    assert(strcmp(model->bios_path,
                  "tests/firmware/main.rom") == 0);
    assert(strcmp(model->logo_path,
                  "tests/firmware/logo.rom") == 0);
    model = model_catalog_find(&catalog, "custom-msx2");
    assert(model);
    assert(model->hardware == MSX_MODEL_GENERIC_MSX2);
    assert(strcmp(model->bios_path, "/firmware/msx2.rom") == 0);
    assert(strcmp(model->subrom_path,
                  "tests/firmware/sub.rom") == 0);
    assert(strcmp(model->disk_rom_path,
                  "tests/firmware/disk.rom") == 0);
    assert(!model_catalog_find(&catalog, "unsupported"));
    assert(model_catalog_index(&catalog, "custom-msx2") == 1);

    edited = catalog.entries[0];
    edited.bios_path[0] = '\0';
    edited.logo_path[0] = '\0';
    edited.subrom_path[0] = '\0';
    edited.disk_rom_path[0] = '\0';
    assert(model_definition_validate(
        &catalog, &edited, 0, true, error, sizeof(error)));
    snprintf(edited.id, sizeof(edited.id), "custom-msx2");
    assert(!model_definition_validate(
        &catalog, &edited, 0, false, error, sizeof(error)));
    assert(strstr(error, "already"));
    snprintf(edited.id, sizeof(edited.id), "invalid model]");
    assert(!model_definition_validate(
        &catalog, &edited, 0, false, error, sizeof(error)));
    assert(strstr(error, "ID must"));
    snprintf(edited.id, sizeof(edited.id), "validated-model");
    snprintf(edited.name, sizeof(edited.name), "Unsafe\nname");
    assert(!model_definition_validate(
        &catalog, &edited, (size_t)-1, false,
        error, sizeof(error)));
    assert(strstr(error, "line breaks"));
    snprintf(edited.name, sizeof(edited.name), "Validated model");
    snprintf(edited.logo_path, sizeof(edited.logo_path),
             "unsafe\npath.rom");
    assert(!model_definition_validate(
        &catalog, &edited, (size_t)-1, false,
        error, sizeof(error)));
    assert(strstr(error, "line breaks"));
    edited.logo_path[0] = '\0';
    snprintf(edited.bios_path, sizeof(edited.bios_path),
             "%s", firmware_path);
    file = fopen(firmware_path, "wb");
    assert(file);
    assert(fputc(0, file) != EOF);
    assert(fclose(file) == 0);
    assert(!model_definition_validate(
        &catalog, &edited, (size_t)-1, true,
        error, sizeof(error)));
    assert(strstr(error, "32 KB"));
    file = fopen(firmware_path, "wb");
    assert(file);
    assert(fseek(file, MSX_BIOS_SIZE - 1, SEEK_SET) == 0);
    assert(fputc(0, file) != EOF);
    assert(fclose(file) == 0);
    assert(model_definition_validate(
        &catalog, &edited, (size_t)-1, true,
        error, sizeof(error)));

    assert(model_catalog_save(&catalog, saved_path) == 0);
    assert(model_catalog_load(&saved, saved_path) == 0);
    assert(saved.count == catalog.count);
    model = model_catalog_find(&saved, "custom-msx");
    assert(model);
    assert(strstr(model->bios_path,
                  "/tests/firmware/main.rom"));

    assert(remove(firmware_path) == 0);
    assert(remove(saved_path) == 0);
    assert(remove(path) == 0);

    puts("machine catalogue tests passed");
    return 0;
}
