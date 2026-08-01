#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "psg.h"
#include "scc.h"
#include "sdcard.h"
#include "types.h"

#define MSX_MEGAFLASH_FLASH_SIZE 0x800000u
#define MSX_MEGAFLASH_RAM_SIZE   0x080000u
#define MSX_MEGAFLASH_CARDS      2u
#define MSX_MEGAFLASH_PATH_MAX   4096u
#define MSX_MEGAFLASH_ERROR_MAX  192u
#define MSX_MEGAFLASH_BUFFER_SIZE 32u

typedef enum {
    MEGAFLASH_FLASH_READ = 0,
    MEGAFLASH_FLASH_AUTOSELECT,
    MEGAFLASH_FLASH_CFI
} MegaFlashReadMode;

typedef struct {
    u8 *flash;
    u8 *ram;
    SdCard cards[MSX_MEGAFLASH_CARDS];
    Psg psg;
    Scc scc;

    u16 bank[4];
    u8 sd_bank[4];
    u8 mapper_segment[4];
    u16 offset;
    u8 mapper;
    u8 config;
    u8 secondary_slot;
    u8 scc_mode;
    u8 scc_bank[4];
    u8 selected_card;
    u8 flash_command_stage;
    u8 flash_program_remaining;
    u8 flash_buffer_expected;
    u8 flash_buffer_written;
    size_t flash_program_page;
    size_t flash_buffer_sector;
    size_t flash_buffer_address[MSX_MEGAFLASH_BUFFER_SIZE];
    u8 flash_buffer_data[MSX_MEGAFLASH_BUFFER_SIZE];
    MegaFlashReadMode flash_read_mode;

    bool loaded;
    bool flash_dirty;
    char persistence_path[MSX_MEGAFLASH_PATH_MAX];
    char persistence_error[MSX_MEGAFLASH_ERROR_MAX];
} MsxMegaFlashRom;

void megaflash_init(MsxMegaFlashRom *mega);
void megaflash_destroy(MsxMegaFlashRom *mega);
void megaflash_reset(MsxMegaFlashRom *mega);
int megaflash_install(MsxMegaFlashRom *mega,
                      const u8 *data, size_t size);
int megaflash_load_persistent(MsxMegaFlashRom *mega,
                              const char *initial_path,
                              const char *state_path);
int megaflash_store_persistent(MsxMegaFlashRom *mega,
                               const char *state_path);
int megaflash_promote_persistent(MsxMegaFlashRom *mega,
                                 const char *pending_path,
                                 const char *state_path);
int megaflash_flush_flash(MsxMegaFlashRom *mega);
int megaflash_eject(MsxMegaFlashRom *mega);
bool megaflash_flash_dirty(const MsxMegaFlashRom *mega);
bool megaflash_flash_has_error(const MsxMegaFlashRom *mega);
const char *megaflash_flash_error(const MsxMegaFlashRom *mega);

bool megaflash_slot_expanded(const MsxMegaFlashRom *mega);
unsigned megaflash_selected_subslot(const MsxMegaFlashRom *mega,
                                    u16 address);
u8 megaflash_secondary_read(const MsxMegaFlashRom *mega);
void megaflash_secondary_write(MsxMegaFlashRom *mega, u8 value);
u8 megaflash_read(MsxMegaFlashRom *mega, u16 address);
void megaflash_write(MsxMegaFlashRom *mega, u16 address, u8 value);

u8 megaflash_mapper_io_read(const MsxMegaFlashRom *mega,
                            unsigned page);
void megaflash_mapper_io_write(MsxMegaFlashRom *mega,
                               unsigned page, u8 value);
void megaflash_psg_io_write(MsxMegaFlashRom *mega,
                            u8 port, u8 value);
void megaflash_render_audio(MsxMegaFlashRom *mega, s16 *sample,
                            unsigned clock_hz, unsigned sample_rate);

int megaflash_mount_card(MsxMegaFlashRom *mega, unsigned card,
                         const char *path, SdImageMode mode);
int megaflash_flush_card(MsxMegaFlashRom *mega, unsigned card);
int megaflash_eject_card(MsxMegaFlashRom *mega, unsigned card);
bool megaflash_card_mounted(const MsxMegaFlashRom *mega, unsigned card);
bool megaflash_card_writable(const MsxMegaFlashRom *mega, unsigned card);
bool megaflash_card_dirty(const MsxMegaFlashRom *mega, unsigned card);
bool megaflash_card_has_error(const MsxMegaFlashRom *mega, unsigned card);
const char *megaflash_card_error(const MsxMegaFlashRom *mega,
                                 unsigned card);
bool megaflash_take_activity(MsxMegaFlashRom *mega, unsigned card);
