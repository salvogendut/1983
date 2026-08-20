#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "sdcard.h"
#include "types.h"

#define MSX_SD_MAPPER_ROM_BANK_SIZE 0x4000u
#define MSX_SD_MAPPER_DRIVER_SIZE 0x20000u
#define MSX_SD_MAPPER_ROM_SIZE 0x40000u
#define MSX_SD_MAPPER_RAM_SIZE 0x80000u
#define MSX_SD_MAPPER_CARDS 2u

typedef struct {
    u8 rom[MSX_SD_MAPPER_ROM_SIZE];
    size_t rom_size;
    u8 ram[MSX_SD_MAPPER_RAM_SIZE];
    SdCard cards[MSX_SD_MAPPER_CARDS];
    u8 secondary_slot;
    u8 mapper_segment[4];
    u8 rom_bank1;
    u8 rom_bank2;
    u8 selected_cards;
    u8 mega_sd_selected_card;
    u16 timer;
    u64 timer_clock_fraction;
    bool mapper_enabled;
    bool alternate_driver;
    bool mega_sd_compat;
    bool rom_loaded;
} MsxSdMapper;

void sd_mapper_init(MsxSdMapper *mapper);
void sd_mapper_destroy(MsxSdMapper *mapper);
void sd_mapper_reset(MsxSdMapper *mapper);

int sd_mapper_install_rom(MsxSdMapper *mapper,
                          const u8 *data, size_t size);
int sd_mapper_eject_rom(MsxSdMapper *mapper);

void sd_mapper_set_mapper_enabled(MsxSdMapper *mapper, bool enabled);
void sd_mapper_set_alternate_driver(MsxSdMapper *mapper, bool alternate);
bool sd_mapper_slot_expanded(const MsxSdMapper *mapper);
unsigned sd_mapper_selected_subslot(const MsxSdMapper *mapper, u16 address);
u8 sd_mapper_secondary_read(const MsxSdMapper *mapper);
void sd_mapper_secondary_write(MsxSdMapper *mapper, u8 value);

u8 sd_mapper_read(MsxSdMapper *mapper, u16 address);
void sd_mapper_write(MsxSdMapper *mapper, u16 address, u8 value);
u8 sd_mapper_io_read(const MsxSdMapper *mapper, unsigned page);
void sd_mapper_io_write(MsxSdMapper *mapper, unsigned page, u8 value);
void sd_mapper_tick(MsxSdMapper *mapper, unsigned cpu_cycles,
                    unsigned cpu_hz);

int sd_mapper_mount_card(MsxSdMapper *mapper, unsigned card,
                         const char *path, SdImageMode mode);
int sd_mapper_flush_card(MsxSdMapper *mapper, unsigned card);
int sd_mapper_eject_card(MsxSdMapper *mapper, unsigned card);
bool sd_mapper_card_mounted(const MsxSdMapper *mapper, unsigned card);
bool sd_mapper_card_writable(const MsxSdMapper *mapper, unsigned card);
bool sd_mapper_card_dirty(const MsxSdMapper *mapper, unsigned card);
bool sd_mapper_card_has_error(const MsxSdMapper *mapper, unsigned card);
const char *sd_mapper_card_error(const MsxSdMapper *mapper, unsigned card);
bool sd_mapper_take_activity(MsxSdMapper *mapper, unsigned card);
