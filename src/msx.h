#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cassette.h"
#include "cartridge.h"
#include "megaflash.h"
#include "psg.h"
#include "rtc.h"
#include "sd_mapper.h"
#include "sunrise.h"
#include "types.h"
#include "vdp.h"
#include "wd2793.h"
#include "z80.h"

#define MSX_CPU_HZ 3579545u
#define MSX_PSG_CLOCK_HZ 1789773u
#define MSX_AUDIO_SAMPLE_RATE 44100u
#define MSX_AUDIO_FRAME_CAPACITY 1024u
#define MSX_RTC_PATH_MAX 4096
#define MSX_RTC_ERROR_MAX 192
#define MSX_NTSC_SCANLINES 262u
#define MSX_PAL_SCANLINES 313u
#define MSX_BIOS_SIZE 0x8000u
#define MSX_LOGO_SIZE 0x4000u
#define MSX_SUBROM_SIZE 0x4000u
#define MSX_DISK_ROM_SIZE 0x4000u
#define MSX_CDX2_ROM_SIZE 0x4000u
#define MSX_RAM_INTERNAL_SIZE 0x20000u
#define MSX_RAM_MAX_SIZE 0x400000u
#define MSX_KEYBOARD_ROWS 11u
#define MSX_KEYBOARD_COLUMNS 8u
#define MSX_JOYSTICK_PORTS 2u
#define MSX_JOY_UP 0x01u
#define MSX_JOY_DOWN 0x02u
#define MSX_JOY_LEFT 0x04u
#define MSX_JOY_RIGHT 0x08u
#define MSX_JOY_TRIGGER_A 0x10u
#define MSX_JOY_TRIGGER_B 0x20u
#define MSX_JOY_MASK 0x3fu

typedef struct {
    bool enabled;
    u8 phase;
    s8 latched_x;
    s8 latched_y;
    int pending_x;
    int pending_y;
    int fractional_x;
    int fractional_y;
    u8 buttons_pressed;
    u64 last_write_cycle;
} MsxMouse;

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

typedef enum {
    MSX_FLOPPY_CONTROLLER_NONE = 0,
    MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793,
    MSX_FLOPPY_CONTROLLER_COUNT
} MsxFloppyController;

typedef struct {
    MsxFloppyController controller;
    int primary_slot;
    int secondary_slot;
} MsxFloppyConfig;

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
} MsxProfile;

typedef bool (*MsxIoExtensionRead)(void *context, u16 port, u8 *value);
typedef bool (*MsxIoExtensionWrite)(void *context, u16 port, u8 value);
typedef void (*MsxIoExtensionReset)(void *context);
typedef void (*MsxIoExtensionAdvance)(void *context, unsigned cycles);

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
    char rtc_persistence_path[MSX_RTC_PATH_MAX];
    char rtc_persistence_error[MSX_RTC_ERROR_MAX];

    u8 bios[MSX_BIOS_SIZE];
    u8 logo[MSX_LOGO_SIZE];
    u8 subrom[MSX_SUBROM_SIZE];
    u8 disk_rom[MSX_DISK_ROM_SIZE];
    u8 *ram;
    size_t ram_capacity;
    u8 internal_ram[MSX_RAM_INTERNAL_SIZE];
    MsxCartridge cartridges[MSX_CARTRIDGE_SLOTS];
    Cassette cassette;
    MsxMegaFlashRom megaflash;
    MsxSdMapper sd_mapper;
    MsxSunriseIde sunrise;
    Wd2793 fdc;
    MsxFloppyConfig floppy_config;
    int megaflash_slot;
    int sd_mapper_slot;
    int sunrise_slot;
    int rs232_slot;
    int cdx2_slot;
    bool bios_loaded;
    bool logo_loaded;
    bool subrom_loaded;
    bool disk_rom_loaded;

    u8 ppi_port_c;
    u8 keyboard_rows[MSX_KEYBOARD_ROWS];
    u8 keyboard_refs[MSX_KEYBOARD_ROWS][MSX_KEYBOARD_COLUMNS];
    u8 joystick_pressed[MSX_JOYSTICK_PORTS];
    MsxMouse mouse[MSX_JOYSTICK_PORTS];

    s16 audio_samples[MSX_AUDIO_FRAME_CAPACITY];
    size_t audio_sample_count;
    u64 audio_sample_cycles;
    bool cassette_audible_monitor;
    int bus_ticked_in_step;

    /* Optional port-mapped host device, such as the TCP/IP UNAPI bridge. */
    void *io_extension_context;
    MsxIoExtensionRead io_extension_read;
    MsxIoExtensionWrite io_extension_write;
    MsxIoExtensionReset io_extension_reset;
    MsxIoExtensionAdvance io_extension_advance;

    /* Port-based cartridge floppy controller (Microsol CDX-2 / disk type 5).
     * Uses the same Wd2793 FDC chip as the built-in controller but at I/O
     * ports D0h-D4h instead of the memory-mapped 3FF8h-3FFFh. */
    bool cdx2_enabled;

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
bool msx_has_memory_mapper(const MsxMachine *msx);
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
void msx_set_io_extension(MsxMachine *msx, void *context,
                          MsxIoExtensionRead read_handler,
                          MsxIoExtensionWrite write_handler,
                          MsxIoExtensionReset reset_handler);
void msx_set_io_extension_advance(MsxMachine *msx, void *context,
                                  MsxIoExtensionAdvance advance_handler);

int msx_install_cdx2(MsxMachine *msx, unsigned slot,
                     const u8 *data, size_t size);
int msx_load_cdx2(MsxMachine *msx, unsigned slot,
                  const char *path);
int msx_eject_cdx2(MsxMachine *msx);
bool msx_cdx2_connected(const MsxMachine *msx);
int msx_cdx2_slot(const MsxMachine *msx);
void msx_reassign_cdx2_slot(MsxMachine *msx, int slot);

u8   msx_memory_read(MsxMachine *msx, u16 address);
void msx_memory_write(MsxMachine *msx, u16 address, u8 value);
u8   msx_io_read(MsxMachine *msx, u16 port);
void msx_io_write(MsxMachine *msx, u16 port, u8 value);

void msx_keyboard_clear(MsxMachine *msx);
void msx_keyboard_press(MsxMachine *msx, unsigned row, unsigned column);
void msx_keyboard_release(MsxMachine *msx, unsigned row, unsigned column);
u8   msx_keyboard_read_row(const MsxMachine *msx, unsigned row);
void msx_joystick_set_pressed(MsxMachine *msx, unsigned port, u8 pressed);
u8   msx_joystick_read_port(const MsxMachine *msx, unsigned port);
void msx_mouse_set_enabled(MsxMachine *msx, unsigned port, bool enabled);
bool msx_mouse_enabled(const MsxMachine *msx, unsigned port);
void msx_mouse_add_host_motion(MsxMachine *msx, unsigned port,
                               int delta_x, int delta_y);
void msx_mouse_set_button(MsxMachine *msx, unsigned port,
                          unsigned button, bool pressed);
void msx_mouse_clear_input(MsxMachine *msx, unsigned port);

int  msx_load_cassette(MsxMachine *msx, const char *path);
void msx_eject_cassette(MsxMachine *msx);
void msx_rewind_cassette(MsxMachine *msx);
bool msx_cassette_mounted(const MsxMachine *msx);
bool msx_cassette_rolling(MsxMachine *msx);
bool msx_cassette_at_end(MsxMachine *msx);
u64  msx_cassette_position_ms(MsxMachine *msx);
u64  msx_cassette_duration_ms(const MsxMachine *msx);
CassetteFileType msx_cassette_file_type(const MsxMachine *msx);
void msx_set_cassette_audible_monitor(MsxMachine *msx, bool enabled);
size_t msx_cassette_waveform_copy(MsxMachine *msx, s16 *samples,
                                  size_t capacity);

int msx_install_bios(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_logo(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_subrom(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_disk_rom(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_cartridge(MsxMachine *msx, const u8 *data, size_t size);
int msx_install_cartridge_slot(MsxMachine *msx, unsigned slot,
                               const u8 *data, size_t size,
                               MsxCartridgeMapper mapper);
int msx_install_rs232(MsxMachine *msx, unsigned slot,
                      const u8 *data, size_t size);
int msx_load_rs232(MsxMachine *msx, unsigned slot,
                   const char *path);
int msx_eject_rs232(MsxMachine *msx);
bool msx_rs232_connected(const MsxMachine *msx);
int msx_rs232_slot(const MsxMachine *msx);
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
int msx_install_sunrise_ide(MsxMachine *msx, unsigned slot,
                            const u8 *data, size_t size);
int msx_load_sunrise_ide(MsxMachine *msx, unsigned slot,
                         const char *path);
int msx_replace_sunrise_ide(MsxMachine *msx, const char *rom_path,
                            const char *disk_path,
                            AtaImageMode mode);
int msx_eject_sunrise_ide(MsxMachine *msx);
bool msx_sunrise_connected(const MsxMachine *msx);
int msx_sunrise_slot(const MsxMachine *msx);
int msx_mount_sunrise_disk(MsxMachine *msx, const char *path);
int msx_mount_sunrise_disk_mode(MsxMachine *msx, const char *path,
                                AtaImageMode mode);
int msx_flush_sunrise_disk(MsxMachine *msx);
int msx_eject_sunrise_disk(MsxMachine *msx);
bool msx_sunrise_disk_mounted(const MsxMachine *msx);
bool msx_sunrise_disk_writable(const MsxMachine *msx);
bool msx_sunrise_disk_dirty(const MsxMachine *msx);
bool msx_sunrise_disk_has_error(const MsxMachine *msx);
const char *msx_sunrise_disk_error(const MsxMachine *msx);
bool msx_sunrise_take_activity(MsxMachine *msx);
int msx_install_sd_mapper(MsxMachine *msx, unsigned slot,
                          const u8 *data, size_t size);
int msx_load_sd_mapper(MsxMachine *msx, unsigned slot,
                       const char *path);
int msx_replace_sd_mapper(
    MsxMachine *msx, const char *rom_path,
    const char *card_a_path, const char *card_b_path,
    SdImageMode mode, bool mapper_enabled, bool alternate_driver);
int msx_eject_sd_mapper(MsxMachine *msx);
bool msx_sd_mapper_connected(const MsxMachine *msx);
int msx_sd_mapper_slot(const MsxMachine *msx);
void msx_sd_mapper_set_ram_enabled(MsxMachine *msx, bool enabled);
void msx_sd_mapper_set_alternate_driver(MsxMachine *msx, bool alternate);
int msx_mount_sd_card(MsxMachine *msx, unsigned card,
                      const char *path, SdImageMode mode);
int msx_flush_sd_card(MsxMachine *msx, unsigned card);
int msx_eject_sd_card(MsxMachine *msx, unsigned card);
bool msx_sd_card_mounted(const MsxMachine *msx, unsigned card);
bool msx_sd_card_writable(const MsxMachine *msx, unsigned card);
bool msx_sd_card_dirty(const MsxMachine *msx, unsigned card);
bool msx_sd_card_has_error(const MsxMachine *msx, unsigned card);
const char *msx_sd_card_error(const MsxMachine *msx, unsigned card);
bool msx_sd_card_take_activity(MsxMachine *msx, unsigned card);
int msx_install_megaflash(MsxMachine *msx, unsigned slot,
                          const u8 *data, size_t size);
int msx_load_megaflash(MsxMachine *msx, unsigned slot,
                       const char *path);
int msx_load_megaflash_persistent(MsxMachine *msx, unsigned slot,
                                   const char *initial_path,
                                   const char *state_path);
int msx_replace_megaflash(
    MsxMachine *msx, const char *initial_path,
    const char *state_path, bool reseed_flash,
    const char *card_a_path, const char *card_b_path,
    SdImageMode mode);
int msx_prepare_megaflash_state(const char *initial_path,
                                const char *state_path);
int msx_commit_megaflash_state(MsxMachine *msx,
                               const char *pending_path,
                               const char *state_path);
int msx_flush_megaflash(MsxMachine *msx);
bool msx_megaflash_flash_dirty(const MsxMachine *msx);
bool msx_megaflash_flash_has_error(const MsxMachine *msx);
const char *msx_megaflash_flash_error(const MsxMachine *msx);
int msx_eject_megaflash(MsxMachine *msx);
bool msx_megaflash_connected(const MsxMachine *msx);
int msx_megaflash_slot(const MsxMachine *msx);
void msx_reassign_extension_slots(MsxMachine *msx,
                                  int sunrise_slot,
                                  int sd_mapper_slot,
                                  int megaflash_slot);
int msx_mount_megaflash_card(MsxMachine *msx, unsigned card,
                             const char *path, SdImageMode mode);
int msx_flush_megaflash_card(MsxMachine *msx, unsigned card);
int msx_eject_megaflash_card(MsxMachine *msx, unsigned card);
bool msx_megaflash_card_mounted(const MsxMachine *msx, unsigned card);
bool msx_megaflash_card_writable(const MsxMachine *msx, unsigned card);
bool msx_megaflash_card_dirty(const MsxMachine *msx, unsigned card);
bool msx_megaflash_card_has_error(const MsxMachine *msx,
                                  unsigned card);
const char *msx_megaflash_card_error(const MsxMachine *msx,
                                     unsigned card);
bool msx_megaflash_take_activity(MsxMachine *msx, unsigned card);
int msx_set_rtc_persistence(MsxMachine *msx, const char *path,
                            u64 host_seconds);
int msx_flush_rtc_persistence(MsxMachine *msx, u64 host_seconds);
bool msx_rtc_persistence_active(const MsxMachine *msx);
bool msx_rtc_persistence_dirty(const MsxMachine *msx);
bool msx_rtc_persistence_has_error(const MsxMachine *msx);
const char *msx_rtc_persistence_error(const MsxMachine *msx);
const char *msx_rtc_persistence_path(const MsxMachine *msx);
const char *msx_floppy_controller_name(MsxFloppyController controller);
const char *msx_floppy_controller_config_name(
    MsxFloppyController controller);
bool msx_floppy_controller_from_name(
    const char *name, MsxFloppyController *controller);
bool msx_floppy_config_valid(MsxModel model,
                             const MsxFloppyConfig *config);
int msx_configure_floppy(MsxMachine *msx,
                         const MsxFloppyConfig *config);
const MsxFloppyConfig *msx_floppy_config(const MsxMachine *msx);
bool msx_floppy_supported(const MsxMachine *msx);
int msx_mount_drive_a(MsxMachine *msx, const char *path,
                      FloppyImageMode mode);
int msx_flush_drive_a(MsxMachine *msx);
int msx_eject_drive_a(MsxMachine *msx);
bool msx_drive_a_mounted(const MsxMachine *msx);
bool msx_drive_a_writable(const MsxMachine *msx);
bool msx_drive_a_dirty(const MsxMachine *msx);
bool msx_drive_a_has_error(const MsxMachine *msx);
const char *msx_drive_a_error(const MsxMachine *msx);
bool msx_drive_a_take_activity(MsxMachine *msx);
int msx_mount_drive_b(MsxMachine *msx, const char *path,
                      FloppyImageMode mode);
int msx_flush_drive_b(MsxMachine *msx);
int msx_eject_drive_b(MsxMachine *msx);
bool msx_drive_b_mounted(const MsxMachine *msx);
bool msx_drive_b_writable(const MsxMachine *msx);
bool msx_drive_b_dirty(const MsxMachine *msx);
bool msx_drive_b_has_error(const MsxMachine *msx);
const char *msx_drive_b_error(const MsxMachine *msx);
bool msx_drive_b_take_activity(MsxMachine *msx);
int msx_load_firmware_set(MsxMachine *msx, const char *bios_path,
                          const char *logo_path,
                          const char *subrom_path,
                          const char *disk_rom_path);
void msx_eject_firmware(MsxMachine *msx);
bool msx_can_boot(const MsxMachine *msx);
