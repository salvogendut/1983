#include "models.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char *path = "tests/test-models.tmp";
    ModelCatalog catalog;
    const ModelDefinition *model;
    FILE *file;

    model_catalog_defaults(&catalog);
    assert(catalog.count == 3);
    assert(strcmp(catalog.entries[0].id, "msx1") == 0);
    assert(catalog.entries[2].hardware ==
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
    assert(remove(path) == 0);

    puts("machine catalogue tests passed");
    return 0;
}
