/* Minimal WASM proof-of-concept main for the 1983 MSX core.
 *
 * Mirrors 1984's JS1984 POC: exposes a tiny C API to the browser glue.
 *   - poc_init():           boot an MSX1 (C-BIOS) from the embedded ROMs
 *   - poc_step():           run one emulated frame (CPU + VDP + PSG)
 *   - poc_pixels():         pointer to the VDP framebuffer (u32 0x00RRGGBB)
 *   - poc_key():            SDL_Scancode key down/up through the MSX matrix
 *   - poc_load_cartridge(): install a game ROM into cartridge slot 1
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

static MsxMachine g_msx;
static KbdHost g_kbd;
static Paste g_paste;
static int g_autorun_frames;
static char g_autorun_command[256];
static bool g_initialized;

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
 * model 0 = MSX1 (C-BIOS), 1 = MSX2 (needs a real MSX2.ROM in the virtual
 * FS; returns -1 when the firmware is absent). May be called repeatedly to
 * switch machines. */
EMSCRIPTEN_KEEPALIVE int poc_init_model(int model, const char *cartridge) {
    MsxModel m;
    MsxRegion region = MSX_REGION_NTSC;

    poc_cancel_paste();
    if (!g_initialized) {
        kbd_init(&g_kbd);
        paste_init(&g_paste);
        g_initialized = true;
    }

    if (model == 1) {
        m = MSX_MODEL_GENERIC_MSX2;
    } else {
        m = MSX_MODEL_GENERIC_MSX1;
    }
    msx_init(&g_msx, m, region, 64);

    if (model == 1) {
        if (msx_load_bios(&g_msx, "roms/MSX2.ROM") != 0)
            return -1;
    } else {
        if (msx_load_bios(&g_msx, "roms/cbios_main_msx1.rom") != 0)
            return -1;
        (void)msx_load_logo(&g_msx, "roms/cbios_logo_msx1.rom");
    }
    if (cartridge && cartridge[0] &&
        msx_load_cartridge(&g_msx, cartridge) != 0)
        return -1;
    return msx_can_boot(&g_msx) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE int poc_init(void) { return poc_init_model(0, NULL); }

EMSCRIPTEN_KEEPALIVE int poc_load_cartridge(const char *path) {
    if (msx_load_cartridge(&g_msx, path) != 0)
        return -1;
    return 0;
}

/* Warm reset of the current machine (keeps loaded ROMs/cartridge). */
EMSCRIPTEN_KEEPALIVE void poc_reset(void) {
    poc_cancel_paste();
    kbd_release_all(&g_kbd, &g_msx);
    msx_reset(&g_msx);
}

EMSCRIPTEN_KEEPALIVE int poc_step(void) {
    if (!g_initialized)
        return -1;
    if (g_autorun_frames > 0 && --g_autorun_frames == 0)
        paste_start(&g_paste, &g_msx, g_autorun_command);
    paste_tick(&g_paste, &g_msx);
    msx_run_frame(&g_msx);
    drain_frame_audio();
    return 0;
}

EMSCRIPTEN_KEEPALIVE unsigned int *poc_pixels(void) {
    return g_msx.vdp.pixels;
}

EMSCRIPTEN_KEEPALIVE int poc_width(void)  { return (int)g_msx.vdp.render_width; }
EMSCRIPTEN_KEEPALIVE int poc_height(void) { return (int)g_msx.vdp.render_height; }

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

/* MSX joystick 1: up/down/left/right = matrix row 7, cols 0-3; button A = row
 * 7 col 6, button B = row 7 col 7. The browser maps a gamepad into this. */
#define MSX_JOY_ROW 7
#define MSX_JOY_COL_A 6
#define MSX_JOY_COL_B 7
EMSCRIPTEN_KEEPALIVE void poc_joy(int col, int pressed) {
    if (col < 0 || col > 7)
        return;
    if (pressed) msx_keyboard_press(&g_msx, MSX_JOY_ROW, (unsigned)col);
    else         msx_keyboard_release(&g_msx, MSX_JOY_ROW, (unsigned)col);
}

/* Diagnostic readback used by the browser to distinguish Gamepad API mapping
 * failures from MSX-side input failures. Row 7 is active low. */
EMSCRIPTEN_KEEPALIVE int poc_joy_matrix(void) {
    return g_msx.keyboard_rows[MSX_JOY_ROW];
}

EMSCRIPTEN_KEEPALIVE int poc_load_disk(const char *path) {
    if (msx_mount_drive_a(&g_msx, path, FLOPPY_IMAGE_READ_WRITE) != 0)
        return -1;
    return 0;
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