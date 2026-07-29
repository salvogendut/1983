#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define MSX_CARTRIDGE_SLOTS 2u
#define MSX_CART_MAX_SIZE   0x400000u

typedef enum {
    MSX_CART_MAPPER_AUTO = 0,
    MSX_CART_MAPPER_LINEAR,
    MSX_CART_MAPPER_ASCII8,
    MSX_CART_MAPPER_ASCII16,
    MSX_CART_MAPPER_KONAMI,
    MSX_CART_MAPPER_KONAMI_SCC,
    MSX_CART_MAPPER_COUNT
} MsxCartridgeMapper;

typedef struct {
    u8 *data;
    size_t size;
    u16 base;
    MsxCartridgeMapper requested_mapper;
    MsxCartridgeMapper mapper;
    u8 banks[4];
    u8 scc_registers[0x100];
    bool loaded;
    bool scc_enabled;
} MsxCartridge;

const char *msx_cartridge_mapper_name(MsxCartridgeMapper mapper);
const char *msx_cartridge_mapper_display_name(MsxCartridgeMapper mapper);
bool msx_cartridge_mapper_from_name(const char *name,
                                    MsxCartridgeMapper *mapper);
MsxCartridgeMapper msx_cartridge_detect_mapper(const u8 *data, size_t size);

void msx_cartridge_init(MsxCartridge *cartridge);
void msx_cartridge_destroy(MsxCartridge *cartridge);
void msx_cartridge_reset(MsxCartridge *cartridge);
void msx_cartridge_eject(MsxCartridge *cartridge);
int msx_cartridge_install(MsxCartridge *cartridge, const u8 *data,
                          size_t size, MsxCartridgeMapper mapper);
int msx_cartridge_set_mapper(MsxCartridge *cartridge,
                             MsxCartridgeMapper mapper);
u8 msx_cartridge_read(const MsxCartridge *cartridge, u16 address);
void msx_cartridge_write(MsxCartridge *cartridge, u16 address, u8 value);
