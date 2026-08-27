#include "sd_mapper.h"

#include <string.h>

static size_t driver_base(const MsxSdMapper *mapper) {
    if (mapper->rom_size <= MSX_SD_MAPPER_DRIVER_SIZE)
        return 0;
    return mapper->alternate_driver ? MSX_SD_MAPPER_DRIVER_SIZE : 0;
}

static bool card_index_valid(unsigned card) {
    return card < MSX_SD_MAPPER_CARDS;
}

static void select_cards(MsxSdMapper *mapper, u8 selected) {
    mapper->selected_cards = selected & 3;
    for (unsigned i = 0; i < MSX_SD_MAPPER_CARDS; ++i)
        sd_card_select(&mapper->cards[i],
                       mapper->selected_cards == (1u << i));
}

static u8 storage_read(MsxSdMapper *mapper, u16 address) {
    size_t offset;

    if (mapper->rom_bank1 == 7 &&
        address >= 0x7b00 && address < 0x7f00) {
        if (mapper->selected_cards == 1)
            return sd_card_transfer(&mapper->cards[0], 0xff);
        if (mapper->selected_cards == 2)
            return sd_card_transfer(&mapper->cards[1], 0xff);
        return 0xff;
    }
    if (address == 0x7ff0) {
        if (!mapper->selected_cards)
            return (mapper->alternate_driver ? 2u : 0u) |
                   (mapper->mapper_enabled ? 1u : 0u);
        for (unsigned i = 0; i < MSX_SD_MAPPER_CARDS; ++i) {
            SdCard *card = &mapper->cards[i];

            if (mapper->selected_cards != (1u << i))
                continue;
            u8 result =
                (sd_card_writable(card) ? 0u : 4u) |
                (sd_card_mounted(card) ? 0u : 2u) |
                (card->media_changed ? 1u : 0u);

            card->media_changed = false;
            return result;
        }
        return 0xff;
    }
    if (address == 0x7ff1)
        return (u8)(mapper->timer >> 8);
    if (address >= 0x4000 && address < 0x8000) {
        offset = driver_base(mapper) +
                 (size_t)mapper->rom_bank1 *
                     MSX_SD_MAPPER_ROM_BANK_SIZE +
                 (address & 0x3fff);
        return offset < mapper->rom_size ? mapper->rom[offset] : 0xff;
    }
    if (address >= 0x8000 && address < 0xc000 &&
        (mapper->rom_bank2 & 8)) {
        offset = driver_base(mapper) +
                 (size_t)(mapper->rom_bank2 & 7) *
                     MSX_SD_MAPPER_ROM_BANK_SIZE +
                 (address & 0x3fff);
        return offset < mapper->rom_size ? mapper->rom[offset] : 0xff;
    }
    return 0xff;
}

static void storage_write(MsxSdMapper *mapper, u16 address, u8 value) {
    if (mapper->rom_bank1 == 7 &&
        address >= 0x7b00 && address < 0x7f00) {
        if (mapper->selected_cards == 1)
            (void)sd_card_transfer(&mapper->cards[0], value);
        else if (mapper->selected_cards == 2)
            (void)sd_card_transfer(&mapper->cards[1], value);
        return;
    }
    if (address == 0x7ff0) {
        select_cards(mapper, value);
        return;
    }
    if (address == 0x7ff1) {
        mapper->timer = ((u16)value << 8) | 0xff;
        mapper->timer_clock_fraction = 0;
        return;
    }
    if ((address & 0xe800) == 0x6000) {
        if (address & 0x1000)
            mapper->rom_bank2 = value & 0x0f;
        else
            mapper->rom_bank1 = value & 0x07;
    }
}

void sd_mapper_init(MsxSdMapper *mapper) {
    if (!mapper)
        return;
    memset(mapper, 0, sizeof(*mapper));
    memset(mapper->rom, 0xff, sizeof(mapper->rom));
    for (unsigned i = 0; i < MSX_SD_MAPPER_CARDS; ++i) {
        sd_card_init(&mapper->cards[i]);
        /* Both the native Nextor and MegaSD windows expose SDHC cards. */
        sd_card_force_high_capacity(&mapper->cards[i], true);
    }
    mapper->mapper_enabled = true;
    sd_mapper_reset(mapper);
}

void sd_mapper_destroy(MsxSdMapper *mapper) {
    if (!mapper)
        return;
    for (unsigned i = 0; i < MSX_SD_MAPPER_CARDS; ++i)
        sd_card_destroy(&mapper->cards[i]);
    memset(mapper, 0, sizeof(*mapper));
}

void sd_mapper_reset(MsxSdMapper *mapper) {
    static const u8 reset_segments[4] = {3, 2, 1, 0};

    if (!mapper)
        return;
    mapper->secondary_slot = 0;
    memcpy(mapper->mapper_segment, reset_segments,
           sizeof(mapper->mapper_segment));
    mapper->rom_bank1 = 0;
    mapper->rom_bank2 = 0;
    mapper->timer = 0;
    mapper->timer_clock_fraction = 0;
    select_cards(mapper, 0);
    for (unsigned i = 0; i < MSX_SD_MAPPER_CARDS; ++i)
        sd_card_reset(&mapper->cards[i]);
}

int sd_mapper_install_rom(MsxSdMapper *mapper,
                          const u8 *data, size_t size) {
    if (!mapper || !data ||
        (size != MSX_SD_MAPPER_DRIVER_SIZE &&
         size != MSX_SD_MAPPER_ROM_SIZE))
        return -1;
    memset(mapper->rom, 0xff, sizeof(mapper->rom));
    memcpy(mapper->rom, data, size);
    mapper->rom_size = size;
    mapper->rom_loaded = true;
    sd_mapper_reset(mapper);
    return 0;
}

int sd_mapper_eject_rom(MsxSdMapper *mapper) {
    if (!mapper)
        return -1;
    for (unsigned i = 0; i < MSX_SD_MAPPER_CARDS; ++i) {
        if (sd_card_eject(&mapper->cards[i]) != 0)
            return -1;
    }
    memset(mapper->rom, 0xff, sizeof(mapper->rom));
    mapper->rom_size = 0;
    mapper->rom_loaded = false;
    sd_mapper_reset(mapper);
    return 0;
}

void sd_mapper_set_mapper_enabled(MsxSdMapper *mapper, bool enabled) {
    if (!mapper || mapper->mapper_enabled == enabled)
        return;
    mapper->mapper_enabled = enabled;
    sd_mapper_reset(mapper);
}

void sd_mapper_set_alternate_driver(MsxSdMapper *mapper, bool alternate) {
    if (!mapper)
        return;
    mapper->alternate_driver = alternate;
}

bool sd_mapper_slot_expanded(const MsxSdMapper *mapper) {
    return mapper && mapper->rom_loaded && mapper->mapper_enabled;
}

unsigned sd_mapper_selected_subslot(const MsxSdMapper *mapper, u16 address) {
    unsigned page = address >> 14;

    return mapper
         ? (mapper->secondary_slot >> (page * 2)) & 3u : 0u;
}

u8 sd_mapper_secondary_read(const MsxSdMapper *mapper) {
    return mapper ? mapper->secondary_slot ^ 0xff : 0xff;
}

void sd_mapper_secondary_write(MsxSdMapper *mapper, u8 value) {
    if (mapper)
        mapper->secondary_slot = value;
}

u8 sd_mapper_read(MsxSdMapper *mapper, u16 address) {
    unsigned subslot;
    unsigned page;
    size_t offset;

    if (!mapper || !mapper->rom_loaded)
        return 0xff;
    if (!mapper->mapper_enabled)
        return storage_read(mapper, address);
    subslot = sd_mapper_selected_subslot(mapper, address);
    if (subslot == 0)
        return storage_read(mapper, address);
    if (subslot != 1)
        return 0xff;
    page = address >> 14;
    offset = (size_t)(mapper->mapper_segment[page] & 0x1f) *
             0x4000 + (address & 0x3fff);
    return mapper->ram[offset];
}

void sd_mapper_write(MsxSdMapper *mapper, u16 address, u8 value) {
    unsigned subslot;
    unsigned page;
    size_t offset;

    if (!mapper || !mapper->rom_loaded)
        return;
    if (!mapper->mapper_enabled) {
        storage_write(mapper, address, value);
        return;
    }
    subslot = sd_mapper_selected_subslot(mapper, address);
    if (subslot == 0) {
        storage_write(mapper, address, value);
        return;
    }
    if (subslot != 1)
        return;
    page = address >> 14;
    offset = (size_t)(mapper->mapper_segment[page] & 0x1f) *
             0x4000 + (address & 0x3fff);
    mapper->ram[offset] = value;
}

u8 sd_mapper_io_read(const MsxSdMapper *mapper, unsigned page) {
    if (!mapper || !mapper->rom_loaded || !mapper->mapper_enabled)
        return 0xff;
    return mapper->mapper_segment[page & 3] | 0xe0;
}

void sd_mapper_io_write(MsxSdMapper *mapper, unsigned page, u8 value) {
    if (mapper && mapper->rom_loaded && mapper->mapper_enabled)
        mapper->mapper_segment[page & 3] = value & 0x1f;
}

void sd_mapper_tick(MsxSdMapper *mapper, unsigned cpu_cycles,
                    unsigned cpu_hz) {
    u64 clocks;

    if (!mapper || !mapper->timer || !cpu_hz)
        return;
    mapper->timer_clock_fraction +=
        (u64)cpu_cycles * 25000000u;
    clocks = mapper->timer_clock_fraction / cpu_hz;
    mapper->timer_clock_fraction %= cpu_hz;
    if (clocks >= mapper->timer)
        mapper->timer = 0;
    else
        mapper->timer -= (u16)clocks;
}

int sd_mapper_mount_card(MsxSdMapper *mapper, unsigned card,
                         const char *path, SdImageMode mode) {
    if (!mapper || !mapper->rom_loaded || !card_index_valid(card))
        return -1;
    return sd_card_mount(&mapper->cards[card], path, mode);
}

int sd_mapper_flush_card(MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card)
         ? sd_card_flush(&mapper->cards[card]) : -1;
}

int sd_mapper_eject_card(MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card)
         ? sd_card_eject(&mapper->cards[card]) : -1;
}

bool sd_mapper_card_mounted(const MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card) &&
           sd_card_mounted(&mapper->cards[card]);
}

bool sd_mapper_card_writable(const MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card) &&
           sd_card_writable(&mapper->cards[card]);
}

bool sd_mapper_card_dirty(const MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card) &&
           sd_card_dirty(&mapper->cards[card]);
}

bool sd_mapper_card_has_error(const MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card) &&
           sd_card_has_error(&mapper->cards[card]);
}

const char *sd_mapper_card_error(const MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card)
         ? sd_card_error(&mapper->cards[card]) : "";
}

bool sd_mapper_take_activity(MsxSdMapper *mapper, unsigned card) {
    return mapper && card_index_valid(card) &&
           sd_card_take_activity(&mapper->cards[card]);
}
