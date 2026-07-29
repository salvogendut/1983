#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cartridge.h"
#include "psg.h"
#include "rtc.h"
#include "types.h"
#include "vdp.h"
#include "z80.h"

#define MSX_CPU_HZ 3579545u
#define MSX_PSG_CLOCK_HZ 1789773u
#define MSX_AUDIO_SAMPLE_RATE 44100u
#define MSX_AUDIO_FRAME_CAPACITY 1024u
#define MSX_NTSC_SCANLINES 262u
#define MSX_PAL_SCANLINES 313u
#define MSX_BIOS_SIZE 0x8000u
#define MSX_LOGO_SIZE 0x4000u
#define MSX_SUBROM_SIZE 0x4000u
#define MSX_DISK_ROM_SIZE 0x4000u
#define MSX_RAM_MAX_SIZE 0x20000u
#define MSX_KEYBOARD_ROWS 11u
#define MSX_KEYBOARD_COLUMNS 8u

typedef enum {
    MSX_MODEL_GENERIC_MSX1 = 0,
    MSX_MODEL_GENERIC_MSX2,
    MSX_MODEL_PHILIPS_NMS8250,
    MSX_MODEL_COUNT
} MsxModel;

typedef enum {
    MSX_REGION_PAL = 0,
    MSX_REGION_NTSC
} MsxRegion;

typedef struct {
    MsxModel   model;
    const char *name;
    int        default_ram_kb;
    int        vram_kb;
    bool       expanded_slots;
    bool       memory_mapper;
    bool       rtc;
    PsgVariant psg_variant;
    bool       requires_subrom;
    bool       requires_disk_rom;
} MsxProfile;

typedef struct {
    const MsxProfile *profile;
    MsxRegion region;
    int       ram_kb;
    int       frame_hz;
    u64       frame;

    /*
     * These registers establish the future memory/I/O boundary. The PPI
     * primary-slot register selects one of four primary slots for each 16K
     * page. MSX2 profiles can additionally expand primary slots into four
     * secondary slots and expose four RAM-mapper segment registers.
     */
    u8 primary_slot;
    u8 secondary_slot[4];
    u8 mapper_segment[4];

    bool paused;
    bool caps_led;
    bool kana_led;

    Z80    cpu;
    Z80Bus bus;
    MsxVdp vdp;
    Psg    psg;
    MsxRtc rtc;

    u8 bios[MSX_BIOS_SIZE];
    u8 logo[MSX_LOGO_SIZE];
    u8 subrom[MSX_SUBROM_SIZE];
    u8 disk_rom[MSX_DISK_ROM_SIZE];
    u8 ram[MSX_RAM_MAX_SIZE];
    MsxCartridge cartridges[MSX_CARTRIDGE_SLOTS];
    bool bios_loaded;
    bool logo_loaded;
    bool subrom_loaded;
    bool disk_rom_loaded;

    u8 ppi_port_c;
    u8 keyboard_rows[MSX_KEYBOARD_ROWS];
    u8 keyboard_refs[MSX_KEYBOARD_ROWS][MSX_KEYBOARD_COLUMNS];

    s16 audio_samples[MSX_AUDIO_FRAME_CAPACITY];
    size_t audio_sample_count;
    u64 audio_sample_cycles;
    int bus_ticked_in_step;

    u64 cycles;
    u64 instructions;
    unsigned cycle_fraction;
    int cycle_balance;
} MsxMachine;

const MsxProfile *msx_profile(MsxModel model);
const char *msx_model_name(MsxModel model);
const char *msx_model_config_name(MsxModel model);
bool msx_model_from_name(const char *name, MsxModel *model);
bool msx_model_is_msx2(MsxModel model);
const char *msx_region_name(MsxRegion region);
const char *msx_vdp_name(const MsxMachine *msx);

int  msx_default_ram_kb(MsxModel model);
int  msx_normalize_ram_kb(MsxModel model, int ram_kb);
int  msx_next_ram_kb(MsxModel model, int ram_kb, int direction);

void msx_init(MsxMachine *msx, MsxModel model, MsxRegion region, int ram_kb);
void msx_destroy(MsxMachine *msx);
void msx_configure(MsxMachine *msx, MsxModel model, MsxRegion region,
                   int ram_kb);
void msx_reset(MsxMachine *msx);
void msx_run_frame(MsxMachine *msx);

u8   msx_memory_read(MsxMachine *msx, u16 address);
void msx_memory_write(MsxMachine *msx, u16 address, u8 value);
u8   msx_io_read(MsxMachine *msx, u16 port);
void msx_io_write(MsxMachine *msx, u16 port, u8 value);

void msx_keyboard_clear(MsxMachine *msx);
void msx_keyboard_press(MsxMachine *msx, unsigned row, unsigned column);
void msx_keyboard_release(MsxMachine *msx, unsigned row, unsigned column);
u8   msx_keyboard_read_row(const MsxMachine *msx, unsigned row);

int msx_install_bios(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_logo(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_subrom(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_disk_rom(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_cartridge(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_cartridge_slot(MsxMachine *msx, unsigned slot,
                               const u8 *data, size_t size,
                               MsxCartridgeMapper mapper);
int msx_load_bios(MsxMachine *msx, const char *path);
int msx_load_logo(MsxMachine *msx, const char *path);
int msx_load_subrom(MsxMachine *msx, const char *path);
int msx_load_disk_rom(MsxMachine *msx, const char *path);
int msx_load_cartridge(MsxMachine *msx, const char *path);
int msx_load_cartridge_slot(MsxMachine *msx, unsigned slot,
                            const char *path, MsxCartridgeMapper mapper);
int msx_set_cartridge_mapper(MsxMachine *msx, unsigned slot,
                             MsxCartridgeMapper mapper);
void msx_eject_cartridge(MsxMachine *msx, unsigned slot);
const MsxCartridge *msx_get_cartridge(const MsxMachine *msx, unsigned slot);
int msx_load_firmware_set(MsxMachine *msx, const char *bios_path,
                          const char *logo_path,
                          const char *subrom_path,
                          const char *disk_rom_path);
void msx_eject_firmware(MsxMachine *msx);
bool msx_can_boot(const MsxMachine *msx);
