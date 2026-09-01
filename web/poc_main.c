/* Minimal WASM proof-of-concept main for the 1983 MSX core.
 *
 * Mirrors 1984's JS1984 POC: exposes a tiny C API to the browser glue.
 *   - poc_init():           boot the Omega MSX2 (RainBIOS) default machine
 *   - poc_step():           run one emulated frame (CPU + VDP + PSG)
 *   - poc_pixels():         pointer to the VDP framebuffer (u32 0x00RRGGBB)
 *   - poc_key():            SDL_Scancode key down/up through the MSX matrix
 *   - poc_load_cartridge_slot(): install a game ROM into either cartridge
 *     slot
 *   - poc_load_disk():      mount a .dsk into drive A from the virtual FS
 *   - poc_load_cassette():  load a CAS tape from the virtual FS
 *   - poc_autorun():        queue RUN"file" after a frame-counted boot delay
 *   - poc_audio_*():        ring-buffer access for the PSG audio (mono s16)
 *
 * No SDL runtime is used — the browser reads the framebuffer and the audio
 * ring buffer directly from wasm memory; the SDL3 headers are only pulled in
 * for the type definitions (KbdHost takes SDL_Scancode and SDL_KeyboardEvent).
 */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "msx.h"
#include "kbd.h"
#include "paste.h"
#include "unapinet.h"

static MsxMachine g_msx;
static KbdHost g_kbd;
static Paste g_paste;
static int g_autorun_frames;
static char g_autorun_command[256];
static bool g_initialized;
static bool g_machine_initialized;
static u8 g_joy_pressed;
static int g_input_device;
static UnapiNet *g_unapinet;
static bool g_sunrise_enabled;
static bool g_scsi_enabled;
static bool g_sd_mapper_enabled;
static bool g_powergraph_enabled;
static MsxVideoSource g_powergraph_video_source = MSX_VIDEO_SOURCE_AUTO;
static bool g_unapi_enabled;
static unsigned g_omega_unified_bank;
static bool g_custom_omega_unified_rom;
static u8 g_custom_omega_rom[MSX_OMEGA_UNIFIED_ROM_SIZE];
static u8 g_scsi_rom[MSX_SCSI_ROM_MAX_SIZE];
static size_t g_scsi_rom_size;
static unsigned g_scsi_target_id = MSX_SCSI_DEFAULT_TARGET_ID;

#define SUNRISE_ROM_PATH "roms/nextor-sunrise.rom"
#define SD_MAPPER_ROM_PATH "roms/sdmapper-v2-nextor.rom"
#define OMEGA_ROM_PATH "roms/rainbios_omega.rom"

static unsigned desired_sd_mapper_slot(void) {
    return (g_sunrise_enabled ? 1u : 0u) +
           (g_scsi_enabled ? 1u : 0u);
}

static unsigned desired_scsi_slot(void) {
    return g_sunrise_enabled ? 1u : 0u;
}

static unsigned desired_powergraph_slot(void) {
    return (g_sunrise_enabled ? 1u : 0u) +
           (g_scsi_enabled ? 1u : 0u) +
           (g_sd_mapper_enabled ? 1u : 0u);
}

static unsigned cartridge_extension_count(void) {
    return (g_sunrise_enabled ? 1u : 0u) +
           (g_scsi_enabled ? 1u : 0u) +
           (g_sd_mapper_enabled ? 1u : 0u) +
           (g_powergraph_enabled ? 1u : 0u);
}

static int rebalance_cartridge_extensions(void) {
    msx_reassign_extension_slots(
        &g_msx,
        g_sunrise_enabled ? 0 : -1,
        g_sd_mapper_enabled ? (int)desired_sd_mapper_slot() : -1,
        -1);
    if (g_scsi_enabled)
        msx_reassign_scsi_slot(&g_msx, (int)desired_scsi_slot());
    if (g_powergraph_enabled &&
        msx_set_powergraph_v9990(
            &g_msx, true, (int)desired_powergraph_slot()) != 0)
        return -1;
    return 0;
}

static int poc_reload_firmware(void) {
    if (g_msx.profile &&
        g_msx.profile->model == MSX_MODEL_GENERIC_MSX2) {
        if (g_custom_omega_unified_rom)
            return msx_install_omega_unified_rom(
                &g_msx, g_custom_omega_rom,
                sizeof(g_custom_omega_rom), g_omega_unified_bank);
        return msx_load_omega_unified_rom(
            &g_msx, OMEGA_ROM_PATH, g_omega_unified_bank);
    }
    if (g_msx.profile &&
        g_msx.profile->model == MSX_MODEL_PHILIPS_NMS8250) {
        return msx_load_firmware_set(
            &g_msx,
            "roms/rainbios_msx2.rom", NULL,
            "roms/rainbios_msx2_sub.rom",
            g_sunrise_enabled ? NULL : "roms/rainbios_nms8250_disk.rom");
    }
    return msx_load_firmware_set(
        &g_msx,
        "roms/cbios_main_msx1.rom",
        "roms/cbios_logo_msx1.rom", NULL, NULL);
}

static void poc_cancel_paste(void) {
    paste_cancel(&g_paste, &g_msx);
    paste_init(&g_paste);
    g_autorun_frames = 0;
    g_autorun_command[0] = '\0';
}

EMSCRIPTEN_KEEPALIVE void poc_key_mod(int scancode, int pressed, int mod);

/* ---- audio ring buffer (mono s16, 4 seconds @ 44.1 kHz) ---- */
#define AUDIO_RING_SAMPLES (44100 * 4)
static s16 g_audio_ring[AUDIO_RING_SAMPLES];
static int g_audio_w = 0;   /* write index (producer = emulator) */
static int g_audio_r = 0;   /* read index  (consumer = browser)  */

static void drain_frame_audio(void) {
    size_t count = g_msx.audio_sample_count;
    const s16 *samples = g_msx.audio_samples;

    for (size_t i = 0; i < count; i++) {
        int w = (g_audio_w + 1) % AUDIO_RING_SAMPLES;
        if (w == g_audio_r) break;            /* full — drop this frame */
        g_audio_ring[g_audio_w] = samples[i];
        g_audio_w = w;
    }
}

EMSCRIPTEN_KEEPALIVE void poc_audio_reset(void) { g_audio_w = 0; g_audio_r = 0; }
EMSCRIPTEN_KEEPALIVE int  poc_audio_avail(void) {
    return (g_audio_w - g_audio_r + AUDIO_RING_SAMPLES) % AUDIO_RING_SAMPLES;
}
EMSCRIPTEN_KEEPALIVE int  poc_audio_read_pos(void) { return g_audio_r; }
EMSCRIPTEN_KEEPALIVE short *poc_audio_buffer(void) { return g_audio_ring; }
EMSCRIPTEN_KEEPALIVE void poc_audio_advance(int n) {
    g_audio_r = (g_audio_r + n) % AUDIO_RING_SAMPLES;
}

/* ---- emulator lifecycle ----
 * model 0 = MSX1 (C-BIOS), 1 = Philips NMS 8250 (RainBIOS), and
 * model 2 = Omega MSX2 (RainBIOS). May be called repeatedly to switch
 * machines. */
EMSCRIPTEN_KEEPALIVE int poc_init_model(int model, const char *cartridge) {
    MsxModel m;
    MsxRegion region = MSX_REGION_NTSC;
    MsxFloppyConfig floppy = {
        .controller = MSX_FLOPPY_CONTROLLER_NONE,
        .primary_slot = -1,
        .secondary_slot = -1,
    };

    if (model < 0 || model > 2)
        return -1;
    poc_cancel_paste();
    if (!g_initialized) {
        kbd_init(&g_kbd);
        paste_init(&g_paste);
        g_unapinet = unapinet_create();
        if (!g_unapinet)
            return -1;
        g_initialized = true;
    }
    if (g_machine_initialized) {
        unapinet_reset(g_unapinet);
        msx_destroy(&g_msx);
    }

    if (model == 1) {
        m = MSX_MODEL_PHILIPS_NMS8250;
        region = MSX_REGION_PAL;
        floppy.controller = MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793;
        floppy.primary_slot = 3;
        floppy.secondary_slot = 3;
    } else if (model == 2) {
        m = MSX_MODEL_GENERIC_MSX2;
        region = MSX_REGION_PAL;
        floppy.controller = MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793;
        floppy.primary_slot = 3;
        floppy.secondary_slot = 3;
    } else if (model == 0) {
        m = MSX_MODEL_GENERIC_MSX1;
    } else {
        return -1;
    }
    msx_init(&g_msx, m, region, msx_default_ram_kb(m));
    g_machine_initialized = true;
    g_joy_pressed = 0;
    if (msx_configure_floppy(&g_msx, &floppy) != 0)
        return -1;

    if (poc_reload_firmware() != 0)
        return -1;
    if (g_sunrise_enabled &&
        msx_load_sunrise_ide(&g_msx, 0, SUNRISE_ROM_PATH) != 0)
        return -1;
    if (g_scsi_enabled &&
        msx_install_scsi(
            &g_msx, desired_scsi_slot(), g_scsi_rom,
            g_scsi_rom_size, g_scsi_target_id) != 0)
        return -1;
    if (g_sd_mapper_enabled) {
        if (msx_load_sd_mapper(
                &g_msx, desired_sd_mapper_slot(), SD_MAPPER_ROM_PATH) != 0)
            return -1;
        msx_sd_mapper_set_ram_enabled(&g_msx, true);
        msx_sd_mapper_set_alternate_driver(&g_msx, false);
    }
    if (g_powergraph_enabled &&
        msx_set_powergraph_v9990(
            &g_msx, true, desired_powergraph_slot()) != 0)
        return -1;
    msx_set_video_source(&g_msx, g_powergraph_video_source);
    msx_set_io_extension(&g_msx, g_unapinet,
                         unapinet_io_read, unapinet_io_write,
                         unapinet_io_reset);
    (void)unapinet_set_enabled(g_unapinet, g_unapi_enabled);
    msx_mouse_set_enabled(&g_msx, 0, g_input_device == 1);
    if (cartridge && cartridge[0] &&
        msx_load_cartridge_slot(
            &g_msx, 0, cartridge, MSX_CART_MAPPER_AUTO) != 0)
        return -1;
    return msx_can_boot(&g_msx) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE int poc_init(void) { return poc_init_model(2, NULL); }

EMSCRIPTEN_KEEPALIVE int poc_ram_kb(void) {
    return g_machine_initialized ? g_msx.ram_kb : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_set_ram_kb(int ram_kb) {
    MsxModel model;

    if (!g_machine_initialized || !g_msx.profile)
        return -1;
    model = g_msx.profile->model;
    if (msx_normalize_ram_kb(model, ram_kb) != ram_kb)
        return -1;
    if (g_msx.ram_kb != ram_kb)
        msx_configure(&g_msx, model, g_msx.region, ram_kb);
    return g_msx.ram_kb == ram_kb ? ram_kb : -1;
}

EMSCRIPTEN_KEEPALIVE int poc_load_cartridge(const char *path) {
    if (msx_load_cartridge_slot(
            &g_msx, 0, path, MSX_CART_MAPPER_AUTO) != 0)
        return -1;
    g_joy_pressed = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_load_cartridge_slot(int slot,
                                                 const char *path) {
    if (slot < 0 || slot >= (int)MSX_CARTRIDGE_SLOTS ||
        msx_load_cartridge_slot(
            &g_msx, (unsigned)slot, path, MSX_CART_MAPPER_AUTO) != 0)
        return -1;
    g_joy_pressed = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_cartridge(int slot) {
    if (slot < 0 || slot >= (int)MSX_CARTRIDGE_SLOTS)
        return;
    msx_eject_cartridge(&g_msx, (unsigned)slot);
    g_joy_pressed = 0;
}

EMSCRIPTEN_KEEPALIVE int poc_cartridge_loaded(int slot) {
    const MsxCartridge *cartridge;

    if (slot < 0 || slot >= (int)MSX_CARTRIDGE_SLOTS)
        return 0;
    cartridge = msx_get_cartridge(&g_msx, (unsigned)slot);
    return cartridge && cartridge->loaded ? 1 : 0;
}

/* Warm reset of the current machine (keeps loaded ROMs/cartridge). */
EMSCRIPTEN_KEEPALIVE void poc_reset(void) {
    poc_cancel_paste();
    kbd_release_all(&g_kbd, &g_msx);
    msx_reset(&g_msx);
    g_joy_pressed = 0;
}

EMSCRIPTEN_KEEPALIVE int poc_omega_unified_bank(void) {
    if (!g_machine_initialized || !g_msx.profile ||
        g_msx.profile->model != MSX_MODEL_GENERIC_MSX2 ||
        !g_msx.unified_rom_loaded)
        return -1;
    return (int)g_omega_unified_bank;
}

/* Install a browser-supplied 512 KiB Omega EEPROM image and cold-boot its
 * lower JP1 bank. The full image is retained in WASM memory so subsequent F3
 * bank changes and machine/extension resets keep using the uploaded ROM. */
EMSCRIPTEN_KEEPALIVE int poc_install_omega_unified_rom(const u8 *data,
                                                        int size) {
    if (!data || size != (int)MSX_OMEGA_UNIFIED_ROM_SIZE ||
        !g_machine_initialized || !g_msx.profile ||
        g_msx.profile->model != MSX_MODEL_GENERIC_MSX2)
        return -1;

    poc_cancel_paste();
    kbd_release_all(&g_kbd, &g_msx);
    if (msx_install_omega_unified_rom(
            &g_msx, data, (size_t)size, 0) != 0)
        return -1;
    memcpy(g_custom_omega_rom, data, sizeof(g_custom_omega_rom));
    g_custom_omega_unified_rom = true;
    g_omega_unified_bank = 0;
    g_audio_w = 0;
    g_audio_r = 0;
    g_joy_pressed = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_flip_omega_unified_bank(void) {
    unsigned bank;

    if (poc_omega_unified_bank() < 0)
        return -1;
    bank = g_omega_unified_bank ^ 1u;
    poc_cancel_paste();
    kbd_release_all(&g_kbd, &g_msx);
    if ((g_custom_omega_unified_rom
             ? msx_install_omega_unified_rom(
                   &g_msx, g_custom_omega_rom,
                   sizeof(g_custom_omega_rom), bank)
             : msx_load_omega_unified_rom(
                   &g_msx, OMEGA_ROM_PATH, bank)) != 0)
        return -1;
    g_omega_unified_bank = bank;
    g_audio_w = 0;
    g_audio_r = 0;
    g_joy_pressed = 0;
    return (int)bank;
}

EMSCRIPTEN_KEEPALIVE int poc_step(void) {
    if (!g_initialized)
        return -1;
    unapinet_poll(g_unapinet);
    if (g_autorun_frames > 0 && --g_autorun_frames == 0)
        paste_start(&g_paste, &g_msx, g_autorun_command);
    paste_tick(&g_paste, &g_msx);
    msx_run_frame(&g_msx);
    drain_frame_audio();
    return 0;
}

EMSCRIPTEN_KEEPALIVE unsigned int *poc_pixels(void) {
    return msx_video_output_is_powergraph(&g_msx)
         ? g_msx.v9990.pixels : g_msx.vdp.pixels;
}

EMSCRIPTEN_KEEPALIVE int poc_width(void) {
    return msx_video_output_is_powergraph(&g_msx)
         ? (int)g_msx.v9990.render_width
         : (int)g_msx.vdp.render_width;
}
EMSCRIPTEN_KEEPALIVE int poc_height(void) {
    return msx_video_output_is_powergraph(&g_msx)
         ? (int)g_msx.v9990.render_height
         : (int)g_msx.vdp.render_height;
}
EMSCRIPTEN_KEEPALIVE int poc_frame_hz(void) { return g_msx.frame_hz; }

EMSCRIPTEN_KEEPALIVE void poc_key(int scancode, int pressed) {
    poc_key_mod(scancode, pressed, 0);
}

/* Like poc_key() but with an explicit keyboard modifier so the browser can
 * synthesize the Shift+F1..F5 chords the MSX function keys use. */
EMSCRIPTEN_KEEPALIVE void poc_key_mod(int scancode, int pressed, int mod) {
    SDL_KeyboardEvent event;

    if (!g_initialized)
        return;
    memset(&event, 0, sizeof(event));
    event.scancode = (SDL_Scancode)scancode;
    event.down = pressed != 0;
    event.repeat = false;
    event.mod = (SDL_Keymod)(unsigned)mod;
    kbd_handle(&g_kbd, &g_msx, &event);
}

/* The browser maps gamepad directions and buttons onto joystick port 1. */
EMSCRIPTEN_KEEPALIVE void poc_joy(int col, int pressed) {
    u8 mask;

    if (col < 0 || col > 5 || g_input_device != 0)
        return;
    mask = (u8)(1u << col);
    if (pressed)
        g_joy_pressed |= mask;
    else
        g_joy_pressed &= (u8)~mask;
    msx_joystick_set_pressed(&g_msx, 0, g_joy_pressed);
}

/* Diagnostic readback used by the browser to distinguish Gamepad API mapping
 * failures from MSX-side input failures. The returned port is active low. */
EMSCRIPTEN_KEEPALIVE int poc_joy_matrix(void) {
    return msx_joystick_read_port(&g_msx, 0);
}

/* device 0 = joystick, 1 = mouse, both on joystick port 1. */
EMSCRIPTEN_KEEPALIVE int poc_set_input_device(int device) {
    if (device != 0 && device != 1)
        return -1;
    g_input_device = device;
    g_joy_pressed = 0;
    msx_joystick_set_pressed(&g_msx, 0, 0);
    msx_mouse_set_enabled(&g_msx, 0, device == 1);
    msx_mouse_clear_input(&g_msx, 0);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_mouse_motion(int delta_x, int delta_y) {
    if (g_input_device == 1)
        msx_mouse_add_host_motion(&g_msx, 0, delta_x, delta_y);
}

EMSCRIPTEN_KEEPALIVE void poc_mouse_button(int button, int pressed) {
    if (g_input_device == 1 && button >= 0 && button <= 1)
        msx_mouse_set_button(
            &g_msx, 0, (unsigned)button, pressed != 0);
}

EMSCRIPTEN_KEEPALIVE void poc_mouse_clear(void) {
    msx_mouse_clear_input(&g_msx, 0);
}

EMSCRIPTEN_KEEPALIVE int poc_load_disk(const char *path) {
    if (msx_mount_drive_a(&g_msx, path, FLOPPY_IMAGE_READ_WRITE) != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_has_floppy(void) {
    const MsxFloppyConfig *floppy = msx_floppy_config(&g_msx);

    return floppy &&
           floppy->controller != MSX_FLOPPY_CONTROLLER_NONE ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_disk(void) {
    (void)msx_eject_drive_a(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_load_cassette(const char *path) {
    if (msx_load_cassette(&g_msx, path) != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_cassette(void) {
    msx_eject_cassette(&g_msx);
}

/* Floppy activity: the FDC motor spins while a disk is being accessed. */
EMSCRIPTEN_KEEPALIVE int poc_disk_motor(void) { return g_msx.fdc.motor ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE void poc_dump_screen_text(void) {
    vdp_dump_screen_text(&g_msx.vdp, stdout);
}

/* ---- browser expansion bay ----
 * Sunrise IDE, MSX SCSI, SD Mapper V2, and PowerGraph V9990 are cartridge devices,
 * assigned to slots I and II in that order while skipping disabled devices.
 * TCP/IP UNAPI is port mapped and reserves no slot. */
EMSCRIPTEN_KEEPALIVE int poc_set_sunrise(int enabled) {
    const bool requested = enabled != 0;

    if (!g_machine_initialized)
        return -1;
    if (requested == g_sunrise_enabled)
        return requested ? 1 : 0;
    if (requested) {
        if (cartridge_extension_count() >= MSX_CARTRIDGE_SLOTS)
            return -1;
        g_sunrise_enabled = true;
        if (rebalance_cartridge_extensions() != 0) {
            g_sunrise_enabled = false;
            return -1;
        }
        msx_eject_cartridge(&g_msx, 0);
        if (msx_load_sunrise_ide(&g_msx, 0, SUNRISE_ROM_PATH) != 0) {
            g_sunrise_enabled = false;
            (void)rebalance_cartridge_extensions();
            return -1;
        }
        if (poc_reload_firmware() != 0)
            return -1;
        return 1;
    }
    if (msx_eject_sunrise_ide(&g_msx) != 0)
        return -1;
    g_sunrise_enabled = false;
    if (rebalance_cartridge_extensions() != 0)
        return -1;
    if (poc_reload_firmware() != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_sunrise_enabled(void) {
    return g_sunrise_enabled && msx_sunrise_connected(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_sunrise_slot(void) {
    return msx_sunrise_slot(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_mount_ide(const char *path, int writable) {
    if (!path || !path[0])
        return -1;
    return msx_mount_sunrise_disk_mode(
        &g_msx, path,
        writable ? ATA_IMAGE_READ_WRITE : ATA_IMAGE_READ_ONLY);
}

EMSCRIPTEN_KEEPALIVE int poc_eject_ide(void) {
    return msx_eject_sunrise_disk(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_ide_mounted(void) {
    return msx_sunrise_disk_mounted(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_ide_writable(void) {
    return msx_sunrise_disk_writable(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_ide_activity(void) {
    return msx_sunrise_take_activity(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_install_scsi_rom(
    const u8 *data, int size, int target_id) {
    if (!data || size <= 0 || size > (int)sizeof(g_scsi_rom) ||
        size % (int)MSX_SCSI_ROM_BANK_SIZE != 0 ||
        target_id < 0 || target_id >= 7 || g_scsi_enabled)
        return -1;
    memcpy(g_scsi_rom, data, (size_t)size);
    g_scsi_rom_size = (size_t)size;
    g_scsi_target_id = (unsigned)target_id;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_rom_ready(void) {
    return g_scsi_rom_size != 0 ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_set_scsi_target_id(int target_id) {
    if (target_id < 0 || target_id >= 7)
        return -1;
    g_scsi_target_id = (unsigned)target_id;
    if (msx_scsi_connected(&g_msx)) {
        msx_scsi_set_target_id(&g_msx.scsi, g_scsi_target_id);
        msx_reset(&g_msx);
    }
    return target_id;
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_target_id(void) {
    return (int)g_scsi_target_id;
}

EMSCRIPTEN_KEEPALIVE int poc_set_scsi(int enabled) {
    const bool requested = enabled != 0;

    if (!g_machine_initialized)
        return -1;
    if (requested == g_scsi_enabled)
        return requested ? 1 : 0;
    if (requested) {
        unsigned slot;

        if (!g_scsi_rom_size ||
            cartridge_extension_count() >= MSX_CARTRIDGE_SLOTS)
            return -1;
        g_scsi_enabled = true;
        if (rebalance_cartridge_extensions() != 0) {
            g_scsi_enabled = false;
            return -1;
        }
        slot = desired_scsi_slot();
        msx_eject_cartridge(&g_msx, slot);
        if (msx_install_scsi(
                &g_msx, slot, g_scsi_rom,
                g_scsi_rom_size, g_scsi_target_id) != 0) {
            g_scsi_enabled = false;
            (void)rebalance_cartridge_extensions();
            return -1;
        }
        return 1;
    }
    if (msx_eject_scsi(&g_msx) != 0)
        return -1;
    g_scsi_enabled = false;
    if (rebalance_cartridge_extensions() != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_enabled(void) {
    return g_scsi_enabled && msx_scsi_connected(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_slot(void) {
    return msx_scsi_slot(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_mount_scsi(const char *path, int writable) {
    if (!path || !path[0])
        return -1;
    return msx_mount_scsi_disk(
        &g_msx, path,
        writable ? ATA_IMAGE_READ_WRITE : ATA_IMAGE_READ_ONLY);
}

EMSCRIPTEN_KEEPALIVE int poc_eject_scsi_disk(void) {
    return msx_eject_scsi_disk(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_disk_mounted(void) {
    return msx_scsi_disk_is_mounted(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_disk_writable(void) {
    return msx_scsi_disk_is_writable(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_scsi_activity(void) {
    return msx_scsi_take_disk_activity(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_set_sd_mapper(int enabled) {
    const bool requested = enabled != 0;

    if (!g_machine_initialized)
        return -1;
    if (requested == g_sd_mapper_enabled)
        return requested ? 1 : 0;
    if (requested) {
        if (cartridge_extension_count() >= MSX_CARTRIDGE_SLOTS)
            return -1;
        unsigned slot;

        g_sd_mapper_enabled = true;
        if (rebalance_cartridge_extensions() != 0) {
            g_sd_mapper_enabled = false;
            return -1;
        }
        slot = desired_sd_mapper_slot();
        msx_eject_cartridge(&g_msx, slot);
        if (msx_load_sd_mapper(&g_msx, slot, SD_MAPPER_ROM_PATH) != 0) {
            g_sd_mapper_enabled = false;
            (void)rebalance_cartridge_extensions();
            return -1;
        }
        msx_sd_mapper_set_ram_enabled(&g_msx, true);
        msx_sd_mapper_set_alternate_driver(&g_msx, false);
        return 1;
    }
    if (msx_eject_sd_mapper(&g_msx) != 0)
        return -1;
    g_sd_mapper_enabled = false;
    if (rebalance_cartridge_extensions() != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_sd_mapper_enabled(void) {
    return g_sd_mapper_enabled && msx_sd_mapper_connected(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_sd_mapper_slot(void) {
    return msx_sd_mapper_slot(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_mount_sd_card(int card, const char *path,
                                           int writable) {
    if (card < 0 || card >= (int)MSX_SD_MAPPER_CARDS ||
        !path || !path[0])
        return -1;
    return msx_mount_sd_card(
        &g_msx, (unsigned)card,
        path, writable ? SD_IMAGE_READ_WRITE : SD_IMAGE_READ_ONLY);
}

EMSCRIPTEN_KEEPALIVE int poc_eject_sd_card(int card) {
    if (card < 0 || card >= (int)MSX_SD_MAPPER_CARDS)
        return -1;
    return msx_eject_sd_card(&g_msx, (unsigned)card);
}

EMSCRIPTEN_KEEPALIVE int poc_sd_card_mounted(int card) {
    if (card < 0 || card >= (int)MSX_SD_MAPPER_CARDS)
        return 0;
    return msx_sd_card_mounted(&g_msx, (unsigned)card) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_sd_activity_mask(void) {
    int mask = 0;

    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card)
        if (msx_sd_card_take_activity(&g_msx, card))
            mask |= 1 << card;
    return mask;
}

EMSCRIPTEN_KEEPALIVE int poc_set_powergraph_v9990(int enabled) {
    const bool requested = enabled != 0;

    if (!g_machine_initialized)
        return -1;
    if (requested == g_powergraph_enabled)
        return requested ? 1 : 0;
    if (requested) {
        unsigned slot;

        if (cartridge_extension_count() >= MSX_CARTRIDGE_SLOTS)
            return -1;
        g_powergraph_enabled = true;
        slot = desired_powergraph_slot();
        if (slot >= MSX_CARTRIDGE_SLOTS ||
            msx_set_powergraph_v9990(&g_msx, true, slot) != 0)
            goto powergraph_failed;
        return 1;
powergraph_failed:
        g_powergraph_enabled = false;
        return -1;
    }
    if (msx_set_powergraph_v9990(&g_msx, false, 0) != 0)
        return -1;
    g_powergraph_enabled = false;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_powergraph_v9990_enabled(void) {
    return g_powergraph_enabled &&
           msx_powergraph_v9990_connected(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_powergraph_v9990_slot(void) {
    return msx_powergraph_v9990_slot(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_set_powergraph_video_source(int source) {
    if (source < 0 || source >= MSX_VIDEO_SOURCE_COUNT)
        return -1;
    g_powergraph_video_source = (MsxVideoSource)source;
    msx_set_video_source(&g_msx, g_powergraph_video_source);
    return source;
}

EMSCRIPTEN_KEEPALIVE int poc_powergraph_video_source(void) {
    return g_powergraph_video_source;
}

EMSCRIPTEN_KEEPALIVE int poc_powergraph_output_active(void) {
    return msx_video_output_is_powergraph(&g_msx) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_set_unapi(int enabled) {
    if (!g_unapinet)
        return -1;
    g_unapi_enabled = enabled != 0;
    if (!unapinet_set_enabled(g_unapinet, g_unapi_enabled))
        return -1;
    return g_unapi_enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_unapi_enabled(void) {
    return unapinet_enabled(g_unapinet) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_unapi_guest_active(void) {
    return unapinet_guest_driver_active(g_unapinet) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_unapi_activity(void) {
    return unapinet_take_activity(g_unapinet) ? 1 : 0;
}

/* A compact protocol probe for the WebAssembly smoke test. */
EMSCRIPTEN_KEEPALIVE int poc_unapi_probe(void) {
    u8 value = 0;

    if (!g_unapinet ||
        !unapinet_io_write(g_unapinet, UNAPINET_COMMAND_PORT, 0x00) ||
        !unapinet_io_read(g_unapinet, UNAPINET_DATA_PORT, &value))
        return -1;
    return value;
}

EMSCRIPTEN_KEEPALIVE void poc_unapi_dns_result(
    int status, int a, int b, int c, int d) {
    const u8 address[4] = {(u8)a, (u8)b, (u8)c, (u8)d};
    unapinet_web_dns_result(g_unapinet, (u8)status, address);
}

EMSCRIPTEN_KEEPALIVE void poc_unapi_tcp_open_result(
    int slot, int status, int a, int b, int c, int d, int port) {
    const u8 address[4] = {(u8)a, (u8)b, (u8)c, (u8)d};
    unapinet_web_tcp_open_result(
        g_unapinet, slot, (u8)status, address, (u16)port);
}

EMSCRIPTEN_KEEPALIVE void poc_unapi_udp_open_result(
    int slot, int status, int port) {
    unapinet_web_udp_open_result(
        g_unapinet, slot, (u8)status, (u16)port);
}

/* Queue RUN"filename" after a frame-counted boot delay, mirroring the native
 * boot-to-BASIC flow instead of synthesizing browser key events. */
EMSCRIPTEN_KEEPALIVE int poc_autorun(const char *filename, int delay_frames) {
    if (!filename || !filename[0])
        return -1;
    size_t len = strlen(filename);
    if (len > 240)
        return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)filename[i];
        if (c < 0x20 || c == 0x7f || c == '"')
            return -1;
    }

    poc_cancel_paste();
    int written = snprintf(g_autorun_command, sizeof(g_autorun_command),
                           "run\"%s", filename);
    if (written < 0 || written >= (int)sizeof(g_autorun_command)) {
        g_autorun_command[0] = '\0';
        return -1;
    }
    g_autorun_frames = delay_frames > 0 ? delay_frames : 42;
    return 0;
}
