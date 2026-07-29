#define _POSIX_C_SOURCE 200809L

#include "megaflash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define MEGAFLASH_FILENO _fileno
#define MEGAFLASH_MKDIR(path) _mkdir(path)
#define MEGAFLASH_SYNC _commit
#else
#include <sys/stat.h>
#include <unistd.h>
#define MEGAFLASH_FILENO fileno
#define MEGAFLASH_MKDIR(path) mkdir((path), 0755)
#define MEGAFLASH_SYNC fsync
#endif

static bool card_valid(unsigned card) {
    return card < MSX_MEGAFLASH_CARDS;
}

static void ensure_parent(const char *path) {
    char copy[MSX_MEGAFLASH_PATH_MAX];
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
            MEGAFLASH_MKDIR(copy);
        *cursor = saved;
        ++cursor;
    }
    if (copy[0])
        MEGAFLASH_MKDIR(copy);
}

static bool memory_mapper_enabled(const MsxMegaFlashRom *mega) {
    return !(mega->config & 0x20);
}

static bool flash_write_enabled(const MsxMegaFlashRom *mega) {
    return (mega->config & 0x01) != 0;
}

static bool recovery_protected(const MsxMegaFlashRom *mega) {
    return (mega->config & 0x02) != 0;
}

static u16 flash_cfi_word(size_t native_address) {
    switch (native_address & 0xff) {
        case 0x00:
            return 0x0020;
        case 0x01:
            return 0x227e;
        case 0x02:
            return 0x2210;
        case 0x03:
            return 0x2200;
        case 0x10:
            return 'Q';
        case 0x11:
            return 'R';
        case 0x12:
            return 'Y';
        case 0x13:
            return 0x02;
        case 0x15:
            return 0x40;
        case 0x1b:
            return 0x27;
        case 0x1c:
            return 0x36;
        case 0x1d:
            return 0xb5;
        case 0x1e:
            return 0xc5;
        case 0x1f:
            return 4;
        case 0x21:
            return 10;
        case 0x23:
            return 4;
        case 0x25:
            return 3;
        case 0x27:
            return 23;
        case 0x28:
            return 2;
        case 0x2a:
            return 5;
        case 0x2c:
            return 2;
        case 0x2d:
            return 7;
        case 0x2f:
            return 0x20;
        case 0x31:
            return 126;
        case 0x34:
            return 1;
        case 0x40:
            return 'P';
        case 0x41:
            return 'R';
        case 0x42:
            return 'I';
        case 0x43:
            return '1';
        case 0x44:
            return '3';
        case 0x46:
            return 2;
        case 0x47:
            return 4;
        case 0x48:
            return 1;
        case 0x49:
            return 4;
        case 0x14:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x20:
        case 0x22:
        case 0x24:
        case 0x26:
        case 0x29:
        case 0x2b:
        case 0x2e:
        case 0x30:
        case 0x32:
        case 0x33:
        case 0x45:
        case 0x4a:
        case 0x4b:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
        case 0x50:
            return 0;
        default:
            return 0xffff;
    }
}

static u8 flash_read(MsxMegaFlashRom *mega, size_t address) {
    size_t native_address;
    u16 cfi;

    address &= MSX_MEGAFLASH_FLASH_SIZE - 1;
    native_address = address >> 1;
    if (mega->flash_read_mode == MEGAFLASH_FLASH_READ)
        return mega->flash[address];
    if (mega->flash_read_mode == MEGAFLASH_FLASH_AUTOSELECT) {
        if (address & 1)
            return 0;
        switch (native_address & 0x7f) {
            case 0:
                return 0x20;
            case 1:
                return 0x7e;
            case 3:
                return 0x08;
            case 0x0e:
                return 0x10;
            case 0x0f:
                return 0x00;
            default:
                return 0;
        }
    }
    cfi = flash_cfi_word(native_address);
    return (u8)(address & 1 ? cfi >> 8 : cfi);
}

static bool unlock_address(size_t address, bool first) {
    size_t native = address >> 1;

    return (native & 0x7ff) == (first ? 0x555u : 0x2aau);
}

static size_t sector_base(size_t address, size_t *size) {
    address &= MSX_MEGAFLASH_FLASH_SIZE - 1;
    if (address < 0x10000) {
        *size = 0x2000;
        return address & ~(size_t)0x1fff;
    }
    *size = 0x10000;
    return address & ~(size_t)0xffff;
}

static void erase_sector(MsxMegaFlashRom *mega, size_t address) {
    size_t size;
    size_t base = sector_base(address, &size);

    if (recovery_protected(mega) && base < 0x4000)
        return;
    memset(mega->flash + base, 0xff, size);
    mega->flash_dirty = true;
}

static void erase_chip(MsxMegaFlashRom *mega) {
    size_t start = recovery_protected(mega) ? 0x4000 : 0;

    memset(mega->flash + start, 0xff,
           MSX_MEGAFLASH_FLASH_SIZE - start);
    mega->flash_dirty = true;
}

static void program_byte(MsxMegaFlashRom *mega,
                         size_t address, u8 value) {
    u8 programmed;

    address &= MSX_MEGAFLASH_FLASH_SIZE - 1;
    if (recovery_protected(mega) && address < 0x4000)
        return;
    programmed = mega->flash[address] & value;
    if (programmed != mega->flash[address]) {
        mega->flash[address] = programmed;
        mega->flash_dirty = true;
    }
}

static void clear_flash_command(MsxMegaFlashRom *mega) {
    mega->flash_command_stage = 0;
    mega->flash_program_remaining = 0;
    mega->flash_buffer_expected = 0;
    mega->flash_buffer_written = 0;
    mega->flash_program_page = 0;
    mega->flash_buffer_sector = 0;
}

static void flash_write(MsxMegaFlashRom *mega,
                        size_t address, u8 value) {
    address &= MSX_MEGAFLASH_FLASH_SIZE - 1;
    if (!flash_write_enabled(mega))
        return;
    if (value == 0xf0) {
        clear_flash_command(mega);
        mega->flash_read_mode = MEGAFLASH_FLASH_READ;
        return;
    }
    if (mega->flash_read_mode != MEGAFLASH_FLASH_READ)
        return;
    switch (mega->flash_command_stage) {
        case 0:
            if (((address >> 1) & 0x7ff) == 0x55 && value == 0x98) {
                mega->flash_read_mode = MEGAFLASH_FLASH_CFI;
            } else if (unlock_address(address, true) && value == 0x50) {
                mega->flash_command_stage = 7;
                mega->flash_program_remaining = 2;
                mega->flash_buffer_written = 0;
            } else if (unlock_address(address, true) && value == 0x56) {
                mega->flash_command_stage = 8;
                mega->flash_program_remaining = 4;
                mega->flash_buffer_written = 0;
            } else if (unlock_address(address, true) && value == 0xaa) {
                mega->flash_command_stage = 1;
            }
            break;
        case 1:
            mega->flash_command_stage =
                unlock_address(address, false) && value == 0x55 ? 2 : 0;
            break;
        case 2:
            mega->flash_command_stage = 0;
            if (value == 0x25) {
                size_t ignored;

                mega->flash_command_stage = 9;
                mega->flash_buffer_sector = sector_base(address, &ignored);
                break;
            }
            if (!unlock_address(address, true))
                break;
            if (value == 0x90)
                mega->flash_read_mode = MEGAFLASH_FLASH_AUTOSELECT;
            else if (value == 0xa0)
                mega->flash_command_stage = 6;
            else if (value == 0x80)
                mega->flash_command_stage = 3;
            break;
        case 3:
            mega->flash_command_stage =
                unlock_address(address, true) && value == 0xaa ? 4 : 0;
            break;
        case 4:
            mega->flash_command_stage =
                unlock_address(address, false) && value == 0x55 ? 5 : 0;
            break;
        case 5:
            mega->flash_command_stage = 0;
            if (value == 0x30)
                erase_sector(mega, address);
            else if (unlock_address(address, true) && value == 0x10)
                erase_chip(mega);
            break;
        case 6:
            mega->flash_command_stage = 0;
            program_byte(mega, address, value);
            break;
        case 7:
        case 8:
            if (mega->flash_buffer_written == 0)
                mega->flash_program_page = address & ~(size_t)7;
            if ((address & ~(size_t)7) != mega->flash_program_page ||
                mega->flash_buffer_written >=
                    MSX_MEGAFLASH_BUFFER_SIZE) {
                clear_flash_command(mega);
                break;
            }
            mega->flash_buffer_address[mega->flash_buffer_written] = address;
            mega->flash_buffer_data[mega->flash_buffer_written++] = value;
            if (--mega->flash_program_remaining == 0) {
                for (unsigned i = 0;
                     i < mega->flash_buffer_written; ++i)
                    program_byte(mega,
                                 mega->flash_buffer_address[i],
                                 mega->flash_buffer_data[i]);
                clear_flash_command(mega);
            }
            break;
        case 9: {
            size_t ignored;

            if (sector_base(address, &ignored) !=
                    mega->flash_buffer_sector ||
                value >= MSX_MEGAFLASH_BUFFER_SIZE) {
                clear_flash_command(mega);
                break;
            }
            mega->flash_buffer_expected = (u8)(value + 1);
            mega->flash_buffer_written = 0;
            mega->flash_command_stage = 10;
            break;
        }
        case 10:
            if (mega->flash_buffer_written == 0)
                mega->flash_program_page =
                    address & ~(size_t)(MSX_MEGAFLASH_BUFFER_SIZE - 1);
            if ((address &
                    ~(size_t)(MSX_MEGAFLASH_BUFFER_SIZE - 1)) !=
                    mega->flash_program_page) {
                clear_flash_command(mega);
                break;
            }
            mega->flash_buffer_address[mega->flash_buffer_written] = address;
            mega->flash_buffer_data[mega->flash_buffer_written++] = value;
            if (mega->flash_buffer_written ==
                    mega->flash_buffer_expected)
                mega->flash_command_stage = 11;
            break;
        case 11: {
            size_t ignored;

            if (value == 0x29 &&
                sector_base(address, &ignored) ==
                    mega->flash_buffer_sector) {
                for (unsigned i = 0;
                     i < mega->flash_buffer_written; ++i)
                    program_byte(mega,
                                 mega->flash_buffer_address[i],
                                 mega->flash_buffer_data[i]);
            }
            clear_flash_command(mega);
            break;
        }
        default:
            clear_flash_command(mega);
            break;
    }
}

static size_t subslot1_flash_address(
    const MsxMegaFlashRom *mega, u16 address) {
    unsigned page;
    unsigned bank;
    unsigned bank_size;

    if ((mega->mapper & 0xc0) == 0x40) {
        page = address >> 14;
        bank_size = 0x4000;
    } else {
        if (address < 0x4000 || address >= 0xc000)
            return (size_t)-1;
        page = (address >> 13) - 2;
        bank_size = 0x2000;
    }
    bank = mega->bank[page];
    if ((mega->config & 0x10) && page == 0 && bank == 0)
        bank = 0x3fa;
    else if ((mega->config & 0x10) && page == 1 && bank == 1)
        bank = 0x3fb;
    else
        bank += mega->offset;
    return ((size_t)bank * bank_size +
            (address & (bank_size - 1)) + 0x10000) &
           (MSX_MEGAFLASH_FLASH_SIZE - 1);
}

static unsigned scc_enable(const MsxMegaFlashRom *mega) {
    if ((mega->scc_mode & 0x20) && (mega->scc_bank[3] & 0x80))
        return 2;
    if (!(mega->scc_mode & 0x20) &&
        (mega->scc_bank[2] & 0x3f) == 0x3f)
        return 1;
    return 0;
}

static u8 read_subslot1(MsxMegaFlashRom *mega, u16 address) {
    size_t flash_address;
    unsigned enable = scc_enable(mega);

    if ((mega->mapper & 0xe0) == 0) {
        if (enable == 1 && address >= 0x9800 && address < 0xa000)
            return scc_read(&mega->scc, (u8)address);
        if (enable == 2 && address >= 0xb800 && address < 0xc000)
            return scc_read(&mega->scc, (u8)address);
    }
    flash_address = subslot1_flash_address(mega, address);
    return flash_address == (size_t)-1
         ? 0xff : flash_read(mega, flash_address);
}

static void write_bank_registers(MsxMegaFlashRom *mega,
                                 u16 address, u8 value) {
    unsigned page = (address >> 13) - 2;

    if (mega->mapper & 0x02 || page >= 4)
        return;
    switch (mega->mapper & 0xe0) {
        case 0x00:
            if ((address & 0x1800) == 0x1000) {
                u8 mask = mega->mapper & 1 ? 0x3f : 0xff;

                mega->scc_bank[page] = value;
                mega->bank[page] = value & mask;
            }
            break;
        case 0x20:
            if ((mega->mapper & 8) && address < 0x6000)
                break;
            if (address < 0x5000 ||
                (address >= 0x5800 && address < 0x6000))
                break;
            mega->bank[page] = value & (mega->mapper & 1
                                      ? 0x1f : 0xff);
            break;
        case 0x40:
        case 0x60:
            mega->bank[page] = value;
            break;
        case 0x80:
        case 0xa0:
            if (address >= 0x6000 && address < 0x8000)
                mega->bank[(address >> 11) & 3] = value;
            break;
        case 0xc0:
        case 0xe0:
            if (address >= 0x6000 && address < 0x6800) {
                mega->bank[0] = (u16)(2 * value) & 0x1ff;
                mega->bank[1] = (u16)(2 * value + 1) & 0x1ff;
            }
            if (address >= 0x7000 && address < 0x7800) {
                mega->bank[2] = (u16)(2 * value) & 0x1ff;
                mega->bank[3] = (u16)(2 * value + 1) & 0x1ff;
            }
            break;
    }
}

static void write_subslot1(MsxMegaFlashRom *mega,
                           u16 address, u8 value) {
    size_t flash_address =
        subslot1_flash_address(mega, address);

    if (!(mega->config & 0x80) && address == 0x7ffc)
        mega->config = value;
    if (!(mega->mapper & 0x04) && address == 0x7fff)
        mega->mapper = value;
    if (!(mega->mapper & 0x02) && address == 0x7ffd)
        mega->offset = (mega->offset & 0x300) | value;
    if (!(mega->mapper & 0x02) && address == 0x7ffe)
        mega->offset = (mega->offset & 0xff) |
                       ((u16)(value & 3) << 8);

    if ((mega->mapper & 0xe0) == 0) {
        unsigned enable;
        bool ram_segment2;
        bool ram_segment3;

        if ((address & 0xfffe) == 0xbffe) {
            mega->scc_mode = value;
            scc_set_mode(&mega->scc, value & 0x20
                         ? SCC_MODE_PLUS : SCC_MODE_COMPATIBLE);
        }
        enable = scc_enable(mega);
        ram_segment2 = (mega->scc_mode & 0x24) == 0x24 ||
                       (mega->scc_mode & 0x10);
        ram_segment3 = (mega->scc_mode & 0x10) != 0;
        if ((enable == 1 && !ram_segment2 &&
             address >= 0x9800 && address < 0xa000) ||
            (enable == 2 && !ram_segment3 &&
             address >= 0xb800 && address < 0xc000)) {
            scc_write(&mega->scc, (u8)address, value);
            return;
        }
    }
    write_bank_registers(mega, address, value);
    if (flash_address != (size_t)-1)
        flash_write(mega, flash_address, value);
}

static size_t ram_address(const MsxMegaFlashRom *mega, u16 address) {
    unsigned page = address >> 14;

    return ((size_t)(mega->mapper_segment[page] & 0x1f) << 14) |
           (address & 0x3fff);
}

static size_t subslot3_flash_address(
    const MsxMegaFlashRom *mega, u16 address) {
    unsigned page = (address >> 13) - 2;

    return 0x700000u +
           (size_t)(mega->sd_bank[page] & 0x7f) * 0x2000u +
           (address & 0x1fff);
}

static void select_sd(MsxMegaFlashRom *mega, bool active) {
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card)
        sd_card_select(&mega->cards[card],
                       active && card == mega->selected_card);
}

static u8 read_subslot3(MsxMegaFlashRom *mega, u16 address) {
    if ((mega->sd_bank[0] & 0xc0) == 0x40 &&
        address >= 0x4000 && address < 0x6000) {
        bool active = !(address & 0x1000);

        select_sd(mega, active);
        return active
             ? sd_card_transfer(&mega->cards[mega->selected_card], 0xff)
             : 0xff;
    }
    if (address >= 0x4000 && address < 0xc000)
        return flash_read(mega,
                          subslot3_flash_address(mega, address));
    return 0xff;
}

static void write_subslot3(MsxMegaFlashRom *mega,
                           u16 address, u8 value) {
    if ((mega->sd_bank[0] & 0xc0) == 0x40 &&
        address >= 0x4000 && address < 0x6000) {
        if (address >= 0x5800) {
            select_sd(mega, false);
            mega->selected_card = value & 1;
        } else {
            bool active = !(address & 0x1000);

            select_sd(mega, active);
            if (active)
                (void)sd_card_transfer(
                    &mega->cards[mega->selected_card], value);
        }
        return;
    }
    if (address >= 0x4000 && address < 0xc000)
        flash_write(mega,
                    subslot3_flash_address(mega, address), value);
    if (address >= 0x6000 && address < 0x8000)
        mega->sd_bank[(address >> 11) & 3] = value;
}

void megaflash_init(MsxMegaFlashRom *mega) {
    if (!mega)
        return;
    memset(mega, 0, sizeof(*mega));
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card)
        sd_card_init(&mega->cards[card]);
    psg_init(&mega->psg, PSG_VARIANT_YM2149);
    psg_set_volume(&mega->psg, 100);
    scc_init(&mega->scc);
    megaflash_reset(mega);
}

void megaflash_destroy(MsxMegaFlashRom *mega) {
    if (!mega)
        return;
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card)
        sd_card_destroy(&mega->cards[card]);
    free(mega->flash);
    free(mega->ram);
    memset(mega, 0, sizeof(*mega));
}

void megaflash_reset(MsxMegaFlashRom *mega) {
    static const u8 mapper_reset[4] = {3, 2, 1, 0};
    static const u8 sd_reset[4] = {0, 1, 0, 0};

    if (!mega)
        return;
    mega->secondary_slot = 0;
    mega->mapper = 0;
    mega->config = 3;
    mega->offset = 0;
    mega->scc_mode = 0;
    mega->selected_card = 0;
    clear_flash_command(mega);
    mega->flash_read_mode = MEGAFLASH_FLASH_READ;
    for (unsigned page = 0; page < 4; ++page) {
        mega->bank[page] = (u16)page;
        mega->scc_bank[page] = (u8)page;
    }
    memcpy(mega->mapper_segment, mapper_reset,
           sizeof(mega->mapper_segment));
    memcpy(mega->sd_bank, sd_reset, sizeof(mega->sd_bank));
    select_sd(mega, false);
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card)
        sd_card_reset(&mega->cards[card]);
    psg_reset(&mega->psg);
    scc_reset(&mega->scc);
}

int megaflash_install(MsxMegaFlashRom *mega,
                      const u8 *data, size_t size) {
    u8 *flash;
    u8 *ram;

    if (!mega || !data || size == 0 ||
        size > MSX_MEGAFLASH_FLASH_SIZE)
        return -1;
    flash = malloc(MSX_MEGAFLASH_FLASH_SIZE);
    ram = calloc(1, MSX_MEGAFLASH_RAM_SIZE);
    if (!flash || !ram) {
        free(flash);
        free(ram);
        return -1;
    }
    memset(flash, 0xff, MSX_MEGAFLASH_FLASH_SIZE);
    memcpy(flash, data, size);
    free(mega->flash);
    free(mega->ram);
    mega->flash = flash;
    mega->ram = ram;
    mega->loaded = true;
    mega->flash_dirty = false;
    mega->persistence_path[0] = '\0';
    mega->persistence_error[0] = '\0';
    megaflash_reset(mega);
    return 0;
}

static int load_flash_file(const char *path, u8 *data,
                           bool missing_allowed, bool exact_size) {
    FILE *file;
    long length;
    size_t got;
    int close_result;

    errno = 0;
    file = path && path[0] ? fopen(path, "rb") : NULL;
    if (!file)
        return missing_allowed && errno == ENOENT ? 1 : -1;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 ||
        length > (long)MSX_MEGAFLASH_FLASH_SIZE ||
        (exact_size &&
         length != (long)MSX_MEGAFLASH_FLASH_SIZE) ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    memset(data, 0xff, MSX_MEGAFLASH_FLASH_SIZE);
    got = fread(data, 1, (size_t)length, file);
    close_result = fclose(file);
    return got == (size_t)length && close_result == 0
         ? 0 : -1;
}

int megaflash_load_persistent(MsxMegaFlashRom *mega,
                              const char *initial_path,
                              const char *state_path) {
    u8 *data;
    int result;

    if (!mega || !initial_path || !initial_path[0] ||
        !state_path || !state_path[0] ||
        strlen(state_path) >= sizeof(mega->persistence_path))
        return -1;
    data = malloc(MSX_MEGAFLASH_FLASH_SIZE);
    if (!data)
        return -1;
    result = load_flash_file(state_path, data, true, true);
    if (result > 0)
        result = load_flash_file(initial_path, data, false, false);
    if (result != 0 || megaflash_install(
            mega, data, MSX_MEGAFLASH_FLASH_SIZE) != 0) {
        snprintf(mega->persistence_error,
                 sizeof(mega->persistence_error),
                 "Cannot load 8 MiB flash state or initial image");
        free(data);
        return -1;
    }
    snprintf(mega->persistence_path,
             sizeof(mega->persistence_path), "%s", state_path);
    mega->persistence_error[0] = '\0';
    free(data);
    return 0;
}

int megaflash_flush_flash(MsxMegaFlashRom *mega) {
    char temporary[MSX_MEGAFLASH_PATH_MAX];
    FILE *file;
    bool failed = false;

    if (!mega || !mega->loaded)
        return -1;
    if (!mega->flash_dirty)
        return 0;
    if (!mega->persistence_path[0] ||
        snprintf(temporary, sizeof(temporary), "%s.tmp",
                 mega->persistence_path) >= (int)sizeof(temporary)) {
        snprintf(mega->persistence_error,
                 sizeof(mega->persistence_error),
                 "No valid persistent flash state path");
        return -1;
    }
    ensure_parent(mega->persistence_path);
    file = fopen(temporary, "wb");
    if (!file) {
        snprintf(mega->persistence_error,
                 sizeof(mega->persistence_error),
                 "Cannot create flash state: %s", strerror(errno));
        return -1;
    }
    if (fwrite(mega->flash, 1, MSX_MEGAFLASH_FLASH_SIZE, file) !=
            MSX_MEGAFLASH_FLASH_SIZE ||
        fflush(file) != 0 ||
        MEGAFLASH_SYNC(MEGAFLASH_FILENO(file)) != 0) {
        snprintf(mega->persistence_error,
                 sizeof(mega->persistence_error),
                 "Cannot flush flash state: %s", strerror(errno));
        failed = true;
    }
    if (fclose(file) != 0) {
        if (!failed)
            snprintf(mega->persistence_error,
                     sizeof(mega->persistence_error),
                     "Cannot close flash state: %s", strerror(errno));
        failed = true;
    }
    if (failed) {
        remove(temporary);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary, mega->persistence_path,
                     MOVEFILE_REPLACE_EXISTING |
                     MOVEFILE_WRITE_THROUGH)) {
        remove(temporary);
        snprintf(mega->persistence_error,
                 sizeof(mega->persistence_error),
                 "Cannot replace persistent flash state");
        return -1;
    }
#else
    if (rename(temporary, mega->persistence_path) != 0) {
        snprintf(mega->persistence_error,
                 sizeof(mega->persistence_error),
                 "Cannot replace flash state: %s", strerror(errno));
        remove(temporary);
        return -1;
    }
#endif
    mega->flash_dirty = false;
    mega->persistence_error[0] = '\0';
    return 0;
}

int megaflash_eject(MsxMegaFlashRom *mega) {
    if (!mega)
        return -1;
    if (mega->flash_dirty && megaflash_flush_flash(mega) != 0)
        return -1;
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (sd_card_mounted(&mega->cards[card]) &&
            sd_card_flush(&mega->cards[card]) != 0)
            return -1;
    }
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (sd_card_eject(&mega->cards[card]) != 0)
            return -1;
    }
    free(mega->flash);
    free(mega->ram);
    mega->flash = NULL;
    mega->ram = NULL;
    mega->loaded = false;
    mega->flash_dirty = false;
    mega->persistence_path[0] = '\0';
    mega->persistence_error[0] = '\0';
    megaflash_reset(mega);
    return 0;
}

bool megaflash_flash_dirty(const MsxMegaFlashRom *mega) {
    return mega && mega->flash_dirty;
}

bool megaflash_flash_has_error(const MsxMegaFlashRom *mega) {
    return mega && mega->persistence_error[0];
}

const char *megaflash_flash_error(const MsxMegaFlashRom *mega) {
    return mega ? mega->persistence_error : "";
}

bool megaflash_slot_expanded(const MsxMegaFlashRom *mega) {
    return mega && mega->loaded && !(mega->config & 0x04);
}

unsigned megaflash_selected_subslot(const MsxMegaFlashRom *mega,
                                    u16 address) {
    if (!mega || (mega->config & 0x04))
        return 1;
    return (mega->secondary_slot >> ((address >> 14) * 2)) & 3;
}

u8 megaflash_secondary_read(const MsxMegaFlashRom *mega) {
    return mega ? mega->secondary_slot ^ 0xff : 0xff;
}

void megaflash_secondary_write(MsxMegaFlashRom *mega, u8 value) {
    if (mega)
        mega->secondary_slot = value;
}

u8 megaflash_read(MsxMegaFlashRom *mega, u16 address) {
    if (!mega || !mega->loaded)
        return 0xff;
    if (megaflash_slot_expanded(mega) && address == 0xffff)
        return megaflash_secondary_read(mega);
    switch (megaflash_selected_subslot(mega, address)) {
        case 0:
            return flash_read(mega, address & 0x3fff);
        case 1:
            return read_subslot1(mega, address);
        case 2:
            return memory_mapper_enabled(mega)
                 ? mega->ram[ram_address(mega, address)] : 0xff;
        case 3:
            return read_subslot3(mega, address);
        default:
            return 0xff;
    }
}

void megaflash_write(MsxMegaFlashRom *mega, u16 address, u8 value) {
    if (!mega || !mega->loaded)
        return;
    if (megaflash_slot_expanded(mega) && address == 0xffff) {
        megaflash_secondary_write(mega, value);
        return;
    }
    switch (megaflash_selected_subslot(mega, address)) {
        case 0:
            flash_write(mega, address & 0x3fff, value);
            break;
        case 1:
            write_subslot1(mega, address, value);
            break;
        case 2:
            if (memory_mapper_enabled(mega))
                mega->ram[ram_address(mega, address)] = value;
            break;
        case 3:
            write_subslot3(mega, address, value);
            break;
    }
}

u8 megaflash_mapper_io_read(const MsxMegaFlashRom *mega,
                            unsigned page) {
    if (!mega || !mega->loaded || !memory_mapper_enabled(mega))
        return 0xff;
    return mega->mapper_segment[page & 3] | 0xe0;
}

void megaflash_mapper_io_write(MsxMegaFlashRom *mega,
                               unsigned page, u8 value) {
    if (mega && mega->loaded && memory_mapper_enabled(mega))
        mega->mapper_segment[page & 3] = value & 0x1f;
}

void megaflash_psg_io_write(MsxMegaFlashRom *mega,
                            u8 port, u8 value) {
    if (!mega || !mega->loaded)
        return;
    if (port == 0x10 || (port == 0xa0 && (mega->config & 8)))
        psg_select(&mega->psg, value);
    else if (port == 0x11 || (port == 0xa1 && (mega->config & 8)))
        psg_write_data(&mega->psg, value);
}

void megaflash_render_audio(MsxMegaFlashRom *mega, s16 *sample,
                            unsigned clock_hz, unsigned sample_rate) {
    s16 psg_sample = 0;
    s16 scc_sample = 0;
    int mixed;

    if (!mega || !mega->loaded || !sample)
        return;
    psg_render(&mega->psg, &psg_sample, 1,
               clock_hz / 2, sample_rate);
    scc_render(&mega->scc, &scc_sample, 1,
               clock_hz, sample_rate);
    mixed = (int)*sample + psg_sample / 2 + scc_sample;
    if (mixed > 32767)
        mixed = 32767;
    else if (mixed < -32768)
        mixed = -32768;
    *sample = (s16)mixed;
}

int megaflash_mount_card(MsxMegaFlashRom *mega, unsigned card,
                         const char *path, SdImageMode mode) {
    return mega && mega->loaded && card_valid(card)
         ? sd_card_mount(&mega->cards[card], path, mode) : -1;
}

int megaflash_flush_card(MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card)
         ? sd_card_flush(&mega->cards[card]) : -1;
}

int megaflash_eject_card(MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card)
         ? sd_card_eject(&mega->cards[card]) : -1;
}

bool megaflash_card_mounted(const MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card) &&
           sd_card_mounted(&mega->cards[card]);
}

bool megaflash_card_writable(const MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card) &&
           sd_card_writable(&mega->cards[card]);
}

bool megaflash_card_dirty(const MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card) &&
           sd_card_dirty(&mega->cards[card]);
}

bool megaflash_card_has_error(const MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card) &&
           sd_card_has_error(&mega->cards[card]);
}

const char *megaflash_card_error(const MsxMegaFlashRom *mega,
                                 unsigned card) {
    return mega && card_valid(card)
         ? sd_card_error(&mega->cards[card]) : "";
}

bool megaflash_take_activity(MsxMegaFlashRom *mega, unsigned card) {
    return mega && card_valid(card) &&
           sd_card_take_activity(&mega->cards[card]);
}
