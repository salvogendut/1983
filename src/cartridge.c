#include "cartridge.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static u16 linear_base(const u8 *data, size_t size) {
    u16 init;

    if (size > 0x8000)
        return 0x0000;
    if (size > 0x4000)
        return 0x4000;
    if (size >= 4 && data[0] == 'A' && data[1] == 'B') {
        init = data[2] | ((u16)data[3] << 8);
        if (init >= 0x8000 && init < 0xc000)
            return 0x8000;
    }
    return 0x4000;
}

const char *msx_cartridge_mapper_name(MsxCartridgeMapper mapper) {
    switch (mapper) {
        case MSX_CART_MAPPER_AUTO:
            return "auto";
        case MSX_CART_MAPPER_LINEAR:
            return "linear";
        case MSX_CART_MAPPER_ASCII8:
            return "ascii8";
        case MSX_CART_MAPPER_ASCII16:
            return "ascii16";
        case MSX_CART_MAPPER_KONAMI:
            return "konami";
        case MSX_CART_MAPPER_KONAMI_SCC:
            return "konami-scc";
        default:
            return "unknown";
    }
}

const char *msx_cartridge_mapper_display_name(MsxCartridgeMapper mapper) {
    switch (mapper) {
        case MSX_CART_MAPPER_AUTO:
            return "Auto";
        case MSX_CART_MAPPER_LINEAR:
            return "Linear";
        case MSX_CART_MAPPER_ASCII8:
            return "ASCII8";
        case MSX_CART_MAPPER_ASCII16:
            return "ASCII16";
        case MSX_CART_MAPPER_KONAMI:
            return "Konami";
        case MSX_CART_MAPPER_KONAMI_SCC:
            return "Konami SCC";
        default:
            return "Unknown";
    }
}

static bool names_equal(const char *left, const char *right) {
    if (!left || !right)
        return false;
    while (*left && *right) {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right))
            return false;
        ++left;
        ++right;
    }
    return !*left && !*right;
}

bool msx_cartridge_mapper_from_name(const char *name,
                                    MsxCartridgeMapper *mapper) {
    if (!name || !mapper)
        return false;
    for (unsigned i = 0; i < MSX_CART_MAPPER_COUNT; ++i) {
        if (names_equal(name,
                        msx_cartridge_mapper_name((MsxCartridgeMapper)i))) {
            *mapper = (MsxCartridgeMapper)i;
            return true;
        }
    }
    return false;
}

MsxCartridgeMapper msx_cartridge_detect_mapper(const u8 *data, size_t size) {
    unsigned scores[MSX_CART_MAPPER_COUNT] = { 0 };
    MsxCartridgeMapper best = MSX_CART_MAPPER_ASCII8;
    unsigned best_score = 0;

    if (!data || !size)
        return MSX_CART_MAPPER_LINEAR;
    if (size < 0x10000)
        return MSX_CART_MAPPER_LINEAR;
    if (size == 0x10000 && (size < 2 || data[0] != 'A' || data[1] != 'B'))
        return MSX_CART_MAPPER_LINEAR;

    /*
     * This is the conservative opcode heuristic used by openMSX when no
     * software-database match is available. It counts LD (nn),A writes to
     * the characteristic mapper-register ranges.
     */
    for (size_t i = 0; i + 2 < size; ++i) {
        u16 address;

        if (data[i] != 0x32)
            continue;
        address = data[i + 1] | ((u16)data[i + 2] << 8);
        switch (address) {
            case 0x5000:
            case 0x9000:
            case 0xb000:
                ++scores[MSX_CART_MAPPER_KONAMI_SCC];
                break;
            case 0x4000:
            case 0x8000:
            case 0xa000:
                ++scores[MSX_CART_MAPPER_KONAMI];
                break;
            case 0x6800:
            case 0x7800:
                ++scores[MSX_CART_MAPPER_ASCII8];
                break;
            case 0x6000:
                ++scores[MSX_CART_MAPPER_KONAMI];
                ++scores[MSX_CART_MAPPER_ASCII8];
                ++scores[MSX_CART_MAPPER_ASCII16];
                break;
            case 0x7000:
                ++scores[MSX_CART_MAPPER_KONAMI_SCC];
                ++scores[MSX_CART_MAPPER_ASCII8];
                ++scores[MSX_CART_MAPPER_ASCII16];
                break;
            case 0x77ff:
                ++scores[MSX_CART_MAPPER_ASCII16];
                break;
            default:
                break;
        }
    }
    if (scores[MSX_CART_MAPPER_ASCII8])
        --scores[MSX_CART_MAPPER_ASCII8];

    for (unsigned i = MSX_CART_MAPPER_ASCII8;
         i <= MSX_CART_MAPPER_KONAMI_SCC; ++i) {
        if (scores[i] >= best_score) {
            best = (MsxCartridgeMapper)i;
            best_score = scores[i];
        }
    }
    return best_score ? best : MSX_CART_MAPPER_ASCII8;
}

static size_t mapper_max_size(MsxCartridgeMapper mapper) {
    switch (mapper) {
        case MSX_CART_MAPPER_LINEAR:
            return 0x10000;
        case MSX_CART_MAPPER_ASCII8:
            return 0x200000;
        case MSX_CART_MAPPER_ASCII16:
            return MSX_CART_MAX_SIZE;
        case MSX_CART_MAPPER_KONAMI:
            return 0x40000;
        case MSX_CART_MAPPER_KONAMI_SCC:
            return 0x80000;
        default:
            return 0;
    }
}

void msx_cartridge_init(MsxCartridge *cartridge) {
    if (!cartridge)
        return;
    memset(cartridge, 0, sizeof(*cartridge));
    cartridge->requested_mapper = MSX_CART_MAPPER_AUTO;
    cartridge->mapper = MSX_CART_MAPPER_LINEAR;
}

void msx_cartridge_destroy(MsxCartridge *cartridge) {
    if (!cartridge)
        return;
    free(cartridge->data);
    msx_cartridge_init(cartridge);
}

void msx_cartridge_reset(MsxCartridge *cartridge) {
    if (!cartridge)
        return;
    memset(cartridge->scc_registers, 0, sizeof(cartridge->scc_registers));
    cartridge->scc_enabled = false;
    if (cartridge->mapper == MSX_CART_MAPPER_KONAMI ||
        cartridge->mapper == MSX_CART_MAPPER_KONAMI_SCC) {
        cartridge->banks[0] = 0;
        cartridge->banks[1] = 1;
        cartridge->banks[2] = 2;
        cartridge->banks[3] = 3;
    } else {
        memset(cartridge->banks, 0, sizeof(cartridge->banks));
    }
}

void msx_cartridge_eject(MsxCartridge *cartridge) {
    msx_cartridge_destroy(cartridge);
}

int msx_cartridge_install(MsxCartridge *cartridge, const u8 *data,
                          size_t size, MsxCartridgeMapper mapper) {
    MsxCartridgeMapper resolved;
    u8 *copy;

    if (!cartridge || !data || !size || size > MSX_CART_MAX_SIZE ||
        (unsigned)mapper >= MSX_CART_MAPPER_COUNT)
        return -1;
    resolved = mapper == MSX_CART_MAPPER_AUTO
             ? msx_cartridge_detect_mapper(data, size) : mapper;
    if (size > mapper_max_size(resolved))
        return -1;
    copy = malloc(size);
    if (!copy)
        return -1;
    memcpy(copy, data, size);

    free(cartridge->data);
    cartridge->data = copy;
    cartridge->size = size;
    cartridge->base = resolved == MSX_CART_MAPPER_LINEAR
                    ? linear_base(data, size) : 0x4000;
    cartridge->requested_mapper = mapper;
    cartridge->mapper = resolved;
    cartridge->loaded = true;
    msx_cartridge_reset(cartridge);
    return 0;
}

int msx_cartridge_set_mapper(MsxCartridge *cartridge,
                             MsxCartridgeMapper mapper) {
    MsxCartridgeMapper resolved;

    if (!cartridge || !cartridge->loaded ||
        (unsigned)mapper >= MSX_CART_MAPPER_COUNT)
        return -1;
    resolved = mapper == MSX_CART_MAPPER_AUTO
             ? msx_cartridge_detect_mapper(cartridge->data, cartridge->size)
             : mapper;
    if (cartridge->size > mapper_max_size(resolved))
        return -1;
    cartridge->requested_mapper = mapper;
    cartridge->mapper = resolved;
    cartridge->base = resolved == MSX_CART_MAPPER_LINEAR
                    ? linear_base(cartridge->data, cartridge->size) : 0x4000;
    msx_cartridge_reset(cartridge);
    return 0;
}

static size_t bank_count(const MsxCartridge *cartridge, size_t bank_size) {
    return (cartridge->size + bank_size - 1) / bank_size;
}

static size_t bank_mask(size_t count) {
    size_t mask = 1;

    while (mask < count)
        mask <<= 1;
    return mask - 1;
}

static size_t selected_bank(const MsxCartridge *cartridge, u8 value,
                            size_t bank_size) {
    size_t count = bank_count(cartridge, bank_size);
    size_t selected = value;
    size_t mask;

    if (cartridge->mapper == MSX_CART_MAPPER_KONAMI)
        mask = 0x1f;
    else
        mask = bank_mask(count);
    if (selected >= count)
        selected &= mask;
    return selected < count ? selected : (size_t)-1;
}

static u8 read_bank(const MsxCartridge *cartridge, u8 bank,
                    size_t bank_size, size_t in_bank) {
    size_t selected = selected_bank(cartridge, bank, bank_size);
    size_t offset;

    if (selected == (size_t)-1)
        return 0xff;
    offset = selected * bank_size + in_bank;
    return offset < cartridge->size ? cartridge->data[offset] : 0xff;
}

u8 msx_cartridge_read(const MsxCartridge *cartridge, u16 address) {
    unsigned window;
    size_t offset;

    if (!cartridge || !cartridge->loaded)
        return 0xff;
    switch (cartridge->mapper) {
        case MSX_CART_MAPPER_LINEAR:
            if (address < cartridge->base)
                return 0xff;
            offset = (size_t)(address - cartridge->base);
            return offset < cartridge->size
                 ? cartridge->data[offset] : 0xff;
        case MSX_CART_MAPPER_ASCII8:
            if (address < 0x4000 || address >= 0xc000)
                return 0xff;
            window = (address - 0x4000) >> 13;
            return read_bank(cartridge, cartridge->banks[window], 0x2000,
                             address & 0x1fff);
        case MSX_CART_MAPPER_ASCII16:
            if (address < 0x4000 || address >= 0xc000)
                return 0xff;
            window = (address - 0x4000) >> 14;
            return read_bank(cartridge, cartridge->banks[window], 0x4000,
                             address & 0x3fff);
        case MSX_CART_MAPPER_KONAMI:
            if (address < 0x4000)
                address += 0x4000;
            else if (address >= 0xc000)
                address -= 0x4000;
            window = (address - 0x4000) >> 13;
            return read_bank(cartridge, cartridge->banks[window], 0x2000,
                             address & 0x1fff);
        case MSX_CART_MAPPER_KONAMI_SCC:
            if (cartridge->scc_enabled &&
                address >= 0x9800 && address < 0xa000)
                return cartridge->scc_registers[address & 0xff];
            if (address < 0x4000)
                address += 0x8000;
            else if (address >= 0xc000)
                address -= 0x8000;
            window = (address - 0x4000) >> 13;
            return read_bank(cartridge, cartridge->banks[window], 0x2000,
                             address & 0x1fff);
        default:
            return 0xff;
    }
}

void msx_cartridge_write(MsxCartridge *cartridge, u16 address, u8 value) {
    unsigned window;

    if (!cartridge || !cartridge->loaded)
        return;
    switch (cartridge->mapper) {
        case MSX_CART_MAPPER_ASCII8:
            if (address >= 0x6000 && address < 0x8000) {
                window = (address >> 11) & 3;
                cartridge->banks[window] = value;
            }
            break;
        case MSX_CART_MAPPER_ASCII16:
            if (address >= 0x6000 && address < 0x7800 &&
                !(address & 0x0800)) {
                window = (address - 0x6000) >> 12;
                cartridge->banks[window] = value;
            }
            break;
        case MSX_CART_MAPPER_KONAMI:
            if (address >= 0x6000 && address < 0xc000) {
                window = (address - 0x4000) >> 13;
                cartridge->banks[window] = value;
            }
            break;
        case MSX_CART_MAPPER_KONAMI_SCC:
            if (cartridge->scc_enabled &&
                address >= 0x9800 && address < 0xa000) {
                cartridge->scc_registers[address & 0xff] = value;
            } else if (address >= 0x5000 && address < 0xc000 &&
                       (address & 0x1800) == 0x1000) {
                window = (address - 0x4000) >> 13;
                cartridge->banks[window] = value;
                if (window == 2)
                    cartridge->scc_enabled = (value & 0x3f) == 0x3f;
            }
            break;
        default:
            break;
    }
}
