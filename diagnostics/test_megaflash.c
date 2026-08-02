#include "megaflash.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *initial_path = "diagnostics/test-megaflash-initial.tmp";
static const char *state_path = "diagnostics/test-megaflash-state.tmp";
static const size_t official_initial_size = 8208384;

static u8 *make_flash(void) {
    u8 *flash = malloc(MSX_MEGAFLASH_FLASH_SIZE);

    assert(flash);
    for (size_t address = 0;
         address < MSX_MEGAFLASH_FLASH_SIZE; ++address)
        flash[address] = (u8)(address >> 13);
    return flash;
}

static void select_subslot(MsxMegaFlashRom *mega, unsigned subslot) {
    u8 value = (u8)(subslot * 0x55);

    megaflash_secondary_write(mega, value);
}

static void test_layout_and_mappers(void) {
    MsxMegaFlashRom mega;
    u8 *flash = make_flash();

    megaflash_init(&mega);
    assert(megaflash_install(
               &mega, flash, MSX_MEGAFLASH_FLASH_SIZE) == 0);
    assert(megaflash_slot_expanded(&mega));
    assert(megaflash_secondary_read(&mega) == 0xff);
    assert(megaflash_read(&mega, 0x0123) ==
           flash[0x0123]);
    assert(megaflash_read(&mega, 0x4123) ==
           flash[0x0123]);

    select_subslot(&mega, 1);
    assert(megaflash_read(&mega, 0x4000) == 8);
    assert(megaflash_read(&mega, 0x6000) == 9);
    megaflash_write(&mega, 0x5000, 6);
    assert(megaflash_read(&mega, 0x4000) == 14);

    megaflash_write(&mega, 0x7fff, 0x80);
    megaflash_write(&mega, 0x6000, 12);
    assert(megaflash_read(&mega, 0x4000) == 20);
    megaflash_write(&mega, 0x7fff, 0xc0);
    megaflash_write(&mega, 0x6000, 20);
    assert(mega.bank[0] == 40);
    assert(mega.bank[1] == 41);

    select_subslot(&mega, 2);
    megaflash_write(&mega, 0x4321, 0x5a);
    assert(megaflash_read(&mega, 0x4321) == 0x5a);
    assert(megaflash_mapper_io_read(&mega, 1) == 0xe2);
    megaflash_mapper_io_write(&mega, 1, 17);
    assert(megaflash_mapper_io_read(&mega, 1) == 0xf1);
    megaflash_write(&mega, 0x4321, 0xa5);
    megaflash_mapper_io_write(&mega, 1, 2);
    assert(megaflash_read(&mega, 0x4321) == 0x5a);

    select_subslot(&mega, 3);
    assert(megaflash_read(&mega, 0x4000) ==
           flash[0x700000]);
    assert(megaflash_read(&mega, 0x6000) ==
           flash[0x702000]);
    megaflash_write(&mega, 0x6000, 0x40);
    assert(mega.sd_bank[0] == 0x40);
    (void)megaflash_read(&mega, 0x4000);
    assert(mega.cards[0].selected);
    (void)megaflash_read(&mega, 0x5000);
    assert(!mega.cards[0].selected);
    megaflash_write(&mega, 0x5800, 1);
    assert(mega.selected_card == 1);

    assert(megaflash_eject(&mega) == 0);
    megaflash_destroy(&mega);
    free(flash);
}

static void test_configuration_flash_and_scc(void) {
    MsxMegaFlashRom mega;
    u8 *flash = make_flash();
    size_t target = 0x11000;

    memset(flash + target, 0xff, MSX_MEGAFLASH_BUFFER_SIZE);
    megaflash_init(&mega);
    assert(megaflash_install(
               &mega, flash, MSX_MEGAFLASH_FLASH_SIZE) == 0);
    select_subslot(&mega, 1);

    megaflash_write(&mega, 0x7ffc, 0x05);
    megaflash_write(&mega, 0x7fff, 0x02);
    assert(!megaflash_slot_expanded(&mega));
    assert(megaflash_selected_subslot(&mega, 0x4000) == 1);
    megaflash_write(&mega, 0x4aaa, 0xaa);
    megaflash_write(&mega, 0x4555, 0x55);
    megaflash_write(&mega, 0x4aaa, 0xa0);
    megaflash_write(&mega, 0x5000, 0x5a);
    megaflash_write(&mega, 0x5000, 0);
    assert(mega.flash[target] == 0x5a);
    assert(mega.flash_dirty);

    megaflash_write(&mega, 0x40aa, 0x98);
    assert(megaflash_read(&mega, 0x4020) == 'Q');
    assert(megaflash_read(&mega, 0x4022) == 'R');
    assert(megaflash_read(&mega, 0x4024) == 'Y');
    megaflash_write(&mega, 0x4000, 0xf0);

    megaflash_write(&mega, 0x4aaa, 0x50);
    megaflash_write(&mega, 0x5001, 0x12);
    megaflash_write(&mega, 0x5002, 0x34);
    assert(mega.flash[target + 1] == 0x12);
    assert(mega.flash[target + 2] == 0x34);

    megaflash_write(&mega, 0x4aaa, 0x56);
    megaflash_write(&mega, 0x5004, 0x45);
    megaflash_write(&mega, 0x5005, 0x67);
    megaflash_write(&mega, 0x5006, 0x89);
    megaflash_write(&mega, 0x5007, 0xab);
    assert(mega.flash[target + 4] == 0x45);
    assert(mega.flash[target + 5] == 0x67);
    assert(mega.flash[target + 6] == 0x89);
    assert(mega.flash[target + 7] == 0xab);

    megaflash_write(&mega, 0x4aaa, 0xaa);
    megaflash_write(&mega, 0x4555, 0x55);
    megaflash_write(&mega, 0x5010, 0x25);
    megaflash_write(&mega, 0x5010, 3);
    megaflash_write(&mega, 0x5010, 0xde);
    megaflash_write(&mega, 0x5011, 0xad);
    megaflash_write(&mega, 0x5012, 0xbe);
    megaflash_write(&mega, 0x5013, 0xef);
    assert(mega.flash[target + 0x10] == 0xff);
    megaflash_write(&mega, 0x5010, 0x29);
    assert(mega.flash[target + 0x10] == 0xde);
    assert(mega.flash[target + 0x11] == 0xad);
    assert(mega.flash[target + 0x12] == 0xbe);
    assert(mega.flash[target + 0x13] == 0xef);

    megaflash_reset(&mega);
    select_subslot(&mega, 1);
    megaflash_write(&mega, 0x9000, 0x3f);
    megaflash_write(&mega, 0x9800, 0x45);
    assert(megaflash_read(&mega, 0x9800) == 0x45);
    megaflash_write(&mega, 0xbffe, 0x20);
    megaflash_write(&mega, 0xb000, 0x80);
    megaflash_write(&mega, 0xb800, 0x67);
    assert(megaflash_read(&mega, 0xb800) == 0x67);

    megaflash_destroy(&mega);
    free(flash);
}

static void write_image_size(const char *path, const u8 *data,
                             size_t size) {
    FILE *file = fopen(path, "wb");

    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void write_image(const char *path, const u8 *data) {
    write_image_size(
        path, data, MSX_MEGAFLASH_FLASH_SIZE);
}

static void program_target(MsxMegaFlashRom *mega, u8 value) {
    select_subslot(mega, 1);
    megaflash_write(mega, 0x7ffc, 0x05);
    megaflash_write(mega, 0x4aaa, 0xaa);
    megaflash_write(mega, 0x4555, 0x55);
    megaflash_write(mega, 0x4aaa, 0xa0);
    megaflash_write(mega, 0x5000, value);
}

static void test_atomic_persistent_flash(void) {
    MsxMegaFlashRom first;
    MsxMegaFlashRom second;
    u8 *flash = make_flash();
    size_t target = 0x11000;

    remove(state_path);
    flash[target] = 0xff;
    write_image_size(initial_path, flash, official_initial_size);
    megaflash_init(&first);
    assert(megaflash_load_persistent(
               &first, initial_path, state_path) == 0);
    assert(first.flash[official_initial_size - 1] ==
           flash[official_initial_size - 1]);
    assert(first.flash[official_initial_size] == 0xff);
    program_target(&first, 0x36);
    assert(megaflash_flash_dirty(&first));
    assert(megaflash_flush_flash(&first) == 0);
    assert(!megaflash_flash_dirty(&first));

    megaflash_init(&second);
    assert(megaflash_load_persistent(
               &second, initial_path, state_path) == 0);
    assert(second.flash[target] == 0x36);
    {
        FILE *source = fopen(initial_path, "rb");
        int byte;

        assert(source);
        assert(fseek(source, (long)target, SEEK_SET) == 0);
        byte = fgetc(source);
        assert(byte == 0xff);
        assert(fclose(source) == 0);
    }
    assert(megaflash_eject(&first) == 0);
    assert(megaflash_eject(&second) == 0);
    megaflash_destroy(&first);
    megaflash_destroy(&second);
    assert(remove(initial_path) == 0);
    assert(remove(state_path) == 0);
    free(flash);
}

static void test_corruption_and_failed_flush_safety(void) {
    const char *corrupt_path = "diagnostics/test-megaflash-corrupt.tmp";
    const char *blocked_parent = "diagnostics/test-megaflash-blocker.tmp";
    const char *blocked_state =
        "diagnostics/test-megaflash-blocker.tmp/state";
    MsxMegaFlashRom mega;
    u8 *flash = make_flash();
    FILE *file;

    remove(state_path);
    remove(corrupt_path);
    remove(blocked_parent);
    write_image(initial_path, flash);
    file = fopen(corrupt_path, "wb");
    assert(file);
    assert(fputc(0x42, file) == 0x42);
    assert(fclose(file) == 0);

    megaflash_init(&mega);
    assert(megaflash_load_persistent(
               &mega, initial_path, corrupt_path) != 0);
    assert(!mega.loaded);
    assert(megaflash_flash_has_error(&mega));

    assert(megaflash_load_persistent(
               &mega, initial_path, state_path) == 0);
    program_target(&mega, 0x24);
    assert(megaflash_flash_dirty(&mega));
    file = fopen(blocked_parent, "wb");
    assert(file);
    assert(fclose(file) == 0);
    snprintf(mega.persistence_path,
             sizeof(mega.persistence_path), "%s", blocked_state);
    assert(megaflash_eject(&mega) != 0);
    assert(mega.loaded);
    assert(megaflash_flash_dirty(&mega));
    assert(megaflash_flash_has_error(&mega));

    snprintf(mega.persistence_path,
             sizeof(mega.persistence_path), "%s", state_path);
    assert(megaflash_eject(&mega) == 0);
    assert(!mega.loaded);
    megaflash_destroy(&mega);
    assert(remove(initial_path) == 0);
    assert(remove(state_path) == 0);
    assert(remove(corrupt_path) == 0);
    assert(remove(blocked_parent) == 0);
    free(flash);
}

int main(void) {
    test_layout_and_mappers();
    test_configuration_flash_and_scc();
    test_atomic_persistent_flash();
    test_corruption_and_failed_flush_safety();
    puts("MegaFlashROM SCC+ SD tests passed");
    return 0;
}
