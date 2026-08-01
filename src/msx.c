#include "msx.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static const MsxProfile profiles[MSX_MODEL_COUNT] = {
    [MSX_MODEL_GENERIC_MSX1] = {
        .model = MSX_MODEL_GENERIC_MSX1,
        .name = "MSX",
        .default_ram_kb = 64,
        .vram_kb = 16,
        .expanded_slots = false,
        .memory_mapper = false,
        .rtc = false,
        .psg_variant = PSG_VARIANT_AY8910,
    },
    [MSX_MODEL_GENERIC_MSX2] = {
        .model = MSX_MODEL_GENERIC_MSX2,
        .name = "MSX2",
        .default_ram_kb = 128,
        .vram_kb = 128,
        .expanded_slots = true,
        .memory_mapper = true,
        .rtc = true,
        .psg_variant = PSG_VARIANT_YM2149,
        .requires_subrom = true,
    },
    [MSX_MODEL_PHILIPS_NMS8250] = {
        .model = MSX_MODEL_PHILIPS_NMS8250,
        .name = "Philips NMS 8250",
        .default_ram_kb = 128,
        .vram_kb = 128,
        .expanded_slots = true,
        .memory_mapper = true,
        .rtc = true,
        .psg_variant = PSG_VARIANT_YM2149,
        .requires_subrom = true,
    },
};

static const int msx1_ram_sizes[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};
static const int msx2_ram_sizes[] = {
    64, 128, 256, 512, 1024, 2048, 4096
};

enum {
    MSX_MOUSE_PHASE_X_HIGH_1 = 0,
    MSX_MOUSE_PHASE_X_LOW_1,
    MSX_MOUSE_PHASE_Y_HIGH_1,
    MSX_MOUSE_PHASE_Y_LOW_1,
    MSX_MOUSE_PHASE_X_HIGH_2,
    MSX_MOUSE_PHASE_X_LOW_2,
    MSX_MOUSE_PHASE_Y_HIGH_2,
    MSX_MOUSE_PHASE_Y_LOW_2,
};

#define MSX_MOUSE_STROBE 0x04u
#define MSX_MOUSE_TIMEOUT_CYCLES \
    ((MSX_CPU_HZ * 1500ULL + 999999ULL) / 1000000ULL)

void msx_keyboard_clear(MsxMachine *msx) {
    if (!msx)
        return;
    memset(msx->keyboard_rows, 0xff, sizeof(msx->keyboard_rows));
    memset(msx->keyboard_refs, 0, sizeof(msx->keyboard_refs));
}

void msx_keyboard_press(MsxMachine *msx, unsigned row, unsigned column) {
    u8 *refs;

    if (!msx || row >= MSX_KEYBOARD_ROWS ||
        column >= MSX_KEYBOARD_COLUMNS)
        return;
    refs = &msx->keyboard_refs[row][column];
    if (*refs != 0xff)
        ++*refs;
    msx->keyboard_rows[row] &= (u8)~(1u << column);
}

void msx_keyboard_release(MsxMachine *msx, unsigned row, unsigned column) {
    u8 *refs;

    if (!msx || row >= MSX_KEYBOARD_ROWS ||
        column >= MSX_KEYBOARD_COLUMNS)
        return;
    refs = &msx->keyboard_refs[row][column];
    if (*refs)
        --*refs;
    if (!*refs)
        msx->keyboard_rows[row] |= (u8)(1u << column);
}

u8 msx_keyboard_read_row(const MsxMachine *msx, unsigned row) {
    if (!msx || row >= MSX_KEYBOARD_ROWS)
        return 0xff;
    return msx->keyboard_rows[row];
}

void msx_joystick_set_pressed(MsxMachine *msx, unsigned port, u8 pressed) {
    if (!msx || port >= MSX_JOYSTICK_PORTS)
        return;
    msx->joystick_pressed[port] = pressed & MSX_JOY_MASK;
}

u8 msx_joystick_read_port(const MsxMachine *msx, unsigned port) {
    if (!msx || port >= MSX_JOYSTICK_PORTS)
        return MSX_JOY_MASK;
    return (u8)(~msx->joystick_pressed[port]) & MSX_JOY_MASK;
}

static u8 msx_mouse_port_output(u8 psg_port_b, unsigned port) {
    return port == 0
         ? (psg_port_b & 0x03) |
           ((psg_port_b & 0x10) >> 2)
         : ((psg_port_b & 0x0c) >> 2) |
           ((psg_port_b & 0x20) >> 3);
}

static void msx_mouse_reset_state(MsxMachine *msx, unsigned port,
                                  bool enabled) {
    MsxMouse *mouse = &msx->mouse[port];

    memset(mouse, 0, sizeof(*mouse));
    mouse->enabled = enabled;
    mouse->phase = MSX_MOUSE_PHASE_Y_LOW_2;
    mouse->last_write_cycle = msx->cycles;
}

void msx_mouse_set_enabled(MsxMachine *msx, unsigned port, bool enabled) {
    if (!msx || port >= MSX_JOYSTICK_PORTS ||
        msx->mouse[port].enabled == enabled)
        return;
    msx_mouse_reset_state(msx, port, enabled);
}

bool msx_mouse_enabled(const MsxMachine *msx, unsigned port) {
    return msx && port < MSX_JOYSTICK_PORTS &&
           msx->mouse[port].enabled;
}

static int msx_mouse_scale_axis(int delta, int *fractional) {
    int total = delta + *fractional;
    int quotient = total / 2;
    int remainder = total % 2;

    if (remainder < 0) {
        --quotient;
        remainder += 2;
    }
    *fractional = remainder;
    return -quotient;
}

void msx_mouse_add_host_motion(MsxMachine *msx, unsigned port,
                               int delta_x, int delta_y) {
    MsxMouse *mouse;

    if (!msx || port >= MSX_JOYSTICK_PORTS ||
        !msx->mouse[port].enabled)
        return;
    mouse = &msx->mouse[port];
    mouse->pending_x +=
        msx_mouse_scale_axis(delta_x, &mouse->fractional_x);
    mouse->pending_y +=
        msx_mouse_scale_axis(delta_y, &mouse->fractional_y);
}

void msx_mouse_set_button(MsxMachine *msx, unsigned port,
                          unsigned button, bool pressed) {
    u8 mask;

    if (!msx || port >= MSX_JOYSTICK_PORTS ||
        !msx->mouse[port].enabled || button > 1)
        return;
    mask = button == 0 ? MSX_JOY_TRIGGER_A : MSX_JOY_TRIGGER_B;
    if (pressed)
        msx->mouse[port].buttons_pressed |= mask;
    else
        msx->mouse[port].buttons_pressed &= (u8)~mask;
}

void msx_mouse_clear_input(MsxMachine *msx, unsigned port) {
    MsxMouse *mouse;

    if (!msx || port >= MSX_JOYSTICK_PORTS)
        return;
    mouse = &msx->mouse[port];
    mouse->latched_x = 0;
    mouse->latched_y = 0;
    mouse->pending_x = 0;
    mouse->pending_y = 0;
    mouse->fractional_x = 0;
    mouse->fractional_y = 0;
    mouse->buttons_pressed = 0;
}

static int msx_mouse_clamp_delta(int delta) {
    if (delta < -127)
        return -127;
    if (delta > 127)
        return 127;
    return delta;
}

static void msx_mouse_latch(MsxMouse *mouse) {
    int delta_x = msx_mouse_clamp_delta(mouse->pending_x);
    int delta_y = msx_mouse_clamp_delta(mouse->pending_y);

    mouse->latched_x = (s8)delta_x;
    mouse->latched_y = (s8)delta_y;
    mouse->pending_x -= delta_x;
    mouse->pending_y -= delta_y;
}

static void msx_mouse_write_port(MsxMachine *msx, unsigned port,
                                 u8 output) {
    MsxMouse *mouse = &msx->mouse[port];
    bool strobe;

    if (!mouse->enabled)
        return;
    if (msx->cycles - mouse->last_write_cycle >
        MSX_MOUSE_TIMEOUT_CYCLES)
        mouse->phase = MSX_MOUSE_PHASE_Y_LOW_2;
    mouse->last_write_cycle = msx->cycles;
    strobe = (output & MSX_MOUSE_STROBE) != 0;

    switch (mouse->phase) {
        case MSX_MOUSE_PHASE_X_HIGH_1:
        case MSX_MOUSE_PHASE_Y_HIGH_1:
        case MSX_MOUSE_PHASE_X_HIGH_2:
        case MSX_MOUSE_PHASE_Y_HIGH_2:
            if (!strobe)
                ++mouse->phase;
            break;
        case MSX_MOUSE_PHASE_X_LOW_1:
        case MSX_MOUSE_PHASE_X_LOW_2:
            if (strobe)
                ++mouse->phase;
            break;
        case MSX_MOUSE_PHASE_Y_LOW_1:
            if (strobe) {
                mouse->phase = MSX_MOUSE_PHASE_X_HIGH_2;
                mouse->latched_x = 0;
                mouse->latched_y = 0;
            }
            break;
        case MSX_MOUSE_PHASE_Y_LOW_2:
            if (strobe) {
                mouse->phase = MSX_MOUSE_PHASE_X_HIGH_1;
                msx_mouse_latch(mouse);
            }
            break;
    }
}

static u8 msx_mouse_read_port(const MsxMouse *mouse) {
    u8 movement;
    u8 status =
        (u8)(~mouse->buttons_pressed) &
        (MSX_JOY_TRIGGER_A | MSX_JOY_TRIGGER_B);

    switch (mouse->phase) {
        case MSX_MOUSE_PHASE_X_HIGH_1:
        case MSX_MOUSE_PHASE_X_HIGH_2:
            movement = ((u8)mouse->latched_x >> 4) & 0x0f;
            break;
        case MSX_MOUSE_PHASE_X_LOW_1:
        case MSX_MOUSE_PHASE_X_LOW_2:
            movement = (u8)mouse->latched_x & 0x0f;
            break;
        case MSX_MOUSE_PHASE_Y_HIGH_1:
        case MSX_MOUSE_PHASE_Y_HIGH_2:
            movement = ((u8)mouse->latched_y >> 4) & 0x0f;
            break;
        case MSX_MOUSE_PHASE_Y_LOW_1:
        case MSX_MOUSE_PHASE_Y_LOW_2:
        default:
            movement = (u8)mouse->latched_y & 0x0f;
            break;
    }
    return movement | status;
}

static u8 msx_joystick_read_psg(MsxMachine *msx) {
    u8 port_b = msx->psg.registers[15];
    unsigned port = (port_b >> 6) & 1u;
    u8 pin_8_mask = port ? 0x20 : 0x10;
    u8 joystick =
        msx->mouse[port].enabled
        ? msx_mouse_read_port(&msx->mouse[port])
        : (port_b & pin_8_mask)
          ? MSX_JOY_MASK : msx_joystick_read_port(msx, port);

    /*
     * Bits 0-5 are the selected joystick's active-low lines. The generic
     * international keyboard layout drives bit 6 low. Bit 7 is the
     * comparator output from the cassette input waveform.
     */
    return (cassette_input(&msx->cassette, msx->cycles)
            ? 0x80 : 0x00) | joystick;
}

static u8 bus_memory_read(void *context, u16 address) {
    return msx_memory_read(context, address);
}

static void bus_memory_write(void *context, u16 address, u8 value) {
    msx_memory_write(context, address, value);
}

static u8 bus_io_read(void *context, u16 port) {
    return msx_io_read(context, port);
}

static void bus_io_write(void *context, u16 port, u8 value) {
    msx_io_write(context, port, value);
}

static void advance_machine(MsxMachine *msx, int cycles) {
    if (!msx || cycles <= 0)
        return;

    msx->cycles += (unsigned)cycles;
    vdp_advance(&msx->vdp, (unsigned)cycles);
    if (msx->profile && msx->profile->rtc)
        rtc_advance(&msx->rtc, (unsigned)cycles, MSX_CPU_HZ);
    sd_mapper_tick(&msx->sd_mapper, (unsigned)cycles, MSX_CPU_HZ);
    msx->audio_sample_cycles +=
        (u64)(unsigned)cycles * MSX_AUDIO_SAMPLE_RATE;
    while (msx->audio_sample_cycles >= MSX_CPU_HZ) {
        s16 discarded;
        s16 *sample =
            msx->audio_sample_count < MSX_AUDIO_FRAME_CAPACITY
            ? &msx->audio_samples[msx->audio_sample_count] : &discarded;

        psg_render(&msx->psg, sample, 1,
                   MSX_PSG_CLOCK_HZ, MSX_AUDIO_SAMPLE_RATE);
        for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot)
            msx_cartridge_render_audio(
                &msx->cartridges[slot], sample,
                MSX_CPU_HZ, MSX_AUDIO_SAMPLE_RATE);
        if (msx_megaflash_connected(msx))
            megaflash_render_audio(
                &msx->megaflash, sample,
                MSX_CPU_HZ, MSX_AUDIO_SAMPLE_RATE);
        if (msx->cassette_audible_monitor) {
            int mixed = (int)*sample +
                cassette_monitor_sample(&msx->cassette, msx->cycles);

            if (mixed > 32767)
                mixed = 32767;
            else if (mixed < -32768)
                mixed = -32768;
            *sample = (s16)mixed;
        }
        if (msx->audio_sample_count < MSX_AUDIO_FRAME_CAPACITY)
            ++msx->audio_sample_count;
        msx->audio_sample_cycles -= MSX_CPU_HZ;
    }
}

static void update_interrupt_line(MsxMachine *msx) {
    /*
     * The VDP interrupt output is level-sensitive. Reading S#0 lowers
     * the line, so an interrupt acknowledged before EI must not survive
     * as a stale edge in the CPU.
     */
    msx->cpu.pending_irq = msx->vdp.irq;
}

static void bus_tick(void *context, int cycles) {
    MsxMachine *msx = context;

    advance_machine(msx, cycles);
    if (msx)
        msx->bus_ticked_in_step += cycles;
}

const MsxProfile *msx_profile(MsxModel model) {
    if ((unsigned)model >= MSX_MODEL_COUNT)
        model = MSX_MODEL_GENERIC_MSX1;
    return &profiles[model];
}

const char *msx_model_name(MsxModel model) {
    return msx_profile(model)->name;
}

const char *msx_model_config_name(MsxModel model) {
    switch (model) {
        case MSX_MODEL_GENERIC_MSX1:
            return "msx1";
        case MSX_MODEL_GENERIC_MSX2:
            return "msx2";
        case MSX_MODEL_PHILIPS_NMS8250:
            return "nms8250";
        case MSX_MODEL_COUNT:
            break;
    }
    return "msx1";
}

bool msx_model_from_name(const char *name, MsxModel *model) {
    if (!name || !model)
        return false;
    if (strcasecmp(name, "msx") == 0 ||
        strcasecmp(name, "msx1") == 0 ||
        strcasecmp(name, "generic-msx1") == 0) {
        *model = MSX_MODEL_GENERIC_MSX1;
        return true;
    }
    if (strcasecmp(name, "msx2") == 0 ||
        strcasecmp(name, "generic-msx2") == 0) {
        *model = MSX_MODEL_GENERIC_MSX2;
        return true;
    }
    if (strcasecmp(name, "nms8250") == 0 ||
        strcasecmp(name, "philips-nms8250") == 0 ||
        strcasecmp(name, "philips_nms_8250") == 0) {
        *model = MSX_MODEL_PHILIPS_NMS8250;
        return true;
    }
    return false;
}

bool msx_model_is_msx2(MsxModel model) {
    return model == MSX_MODEL_GENERIC_MSX2 ||
           model == MSX_MODEL_PHILIPS_NMS8250;
}

const char *msx_region_name(MsxRegion region) {
    return region == MSX_REGION_NTSC ? "NTSC 60 Hz" : "PAL 50 Hz";
}

const char *msx_vdp_name(const MsxMachine *msx) {
    if (msx && msx_model_is_msx2(msx->profile->model))
        return "V9938";
    return msx && msx->region == MSX_REGION_NTSC
         ? "TMS9918A" : "TMS9929A";
}

int msx_default_ram_kb(MsxModel model) {
    return msx_profile(model)->default_ram_kb;
}

static const int *ram_sizes(MsxModel model, size_t *count) {
    if (msx_model_is_msx2(model)) {
        *count = sizeof(msx2_ram_sizes) / sizeof(msx2_ram_sizes[0]);
        return msx2_ram_sizes;
    }
    *count = sizeof(msx1_ram_sizes) / sizeof(msx1_ram_sizes[0]);
    return msx1_ram_sizes;
}

int msx_normalize_ram_kb(MsxModel model, int ram_kb) {
    size_t count = 0;
    const int *sizes = ram_sizes(model, &count);
    int best = sizes[0];

    for (size_t i = 0; i < count; ++i) {
        if (ram_kb == sizes[i])
            return ram_kb;
        if (ram_kb > sizes[i])
            best = sizes[i];
    }
    return best;
}

int msx_next_ram_kb(MsxModel model, int ram_kb, int direction) {
    size_t count = 0;
    const int *sizes = ram_sizes(model, &count);
    int normalized = msx_normalize_ram_kb(model, ram_kb);
    size_t index = 0;

    while (index + 1 < count && sizes[index] != normalized)
        ++index;
    if (direction >= 0)
        index = (index + 1) % count;
    else
        index = index == 0 ? count - 1 : index - 1;
    return sizes[index];
}

static int configure_ram_storage(MsxMachine *msx, int ram_kb) {
    size_t required = (size_t)ram_kb * 1024;

    if (required <= sizeof(msx->internal_ram)) {
        if (msx->ram && msx->ram != msx->internal_ram)
            free(msx->ram);
        msx->ram = msx->internal_ram;
        msx->ram_capacity = sizeof(msx->internal_ram);
        return 0;
    }
    if (required > MSX_RAM_MAX_SIZE)
        return -1;
    if (msx->ram != msx->internal_ram &&
        msx->ram_capacity == required)
        return 0;
    {
        u8 *expanded = malloc(required);

        if (!expanded)
            return -1;
        if (msx->ram && msx->ram != msx->internal_ram)
            free(msx->ram);
        msx->ram = expanded;
        msx->ram_capacity = required;
    }
    return 0;
}

bool msx_has_memory_mapper(const MsxMachine *msx) {
    return msx && msx->profile &&
           (msx->profile->memory_mapper || msx->ram_kb > 64);
}

void msx_reset(MsxMachine *msx) {
    if (!msx)
        return;
    msx->frame = 0;
    msx->primary_slot = 0;
    memset(msx->secondary_slot, 0, sizeof(msx->secondary_slot));
    memset(msx->mapper_segment, 0, sizeof(msx->mapper_segment));
    for (unsigned i = 0; i < MSX_CARTRIDGE_SLOTS; ++i)
        msx_cartridge_reset(&msx->cartridges[i]);
    megaflash_reset(&msx->megaflash);
    sd_mapper_reset(&msx->sd_mapper);
    sunrise_reset(&msx->sunrise);
    wd2793_reset(&msx->fdc);
    msx->paused = false;
    msx->caps_led = false;
    msx->kana_led = false;
    msx->ppi_port_c = 0xff;
    cassette_reset(&msx->cassette, msx->cycles);
    msx_keyboard_clear(msx);
    memset(msx->joystick_pressed, 0, sizeof(msx->joystick_pressed));
    msx->cycles = 0;
    psg_reset(&msx->psg);
    rtc_reset(&msx->rtc);
    /*
     * Standard MSX wiring treats PSG port A as input and port B as output,
     * even on engines which ignore software attempts to reverse them.
     */
    psg_write_register(&msx->psg, 7, 0x80);
    for (unsigned port = 0; port < MSX_JOYSTICK_PORTS; ++port)
        msx_mouse_reset_state(
            msx, port, msx->mouse[port].enabled);
    if (msx->ram)
        memset(msx->ram, 0, msx->ram_capacity);
    msx->audio_sample_count = 0;
    msx->audio_sample_cycles = 0;
    msx->bus_ticked_in_step = 0;
    msx->instructions = 0;
    msx->cycle_fraction = 0;
    msx->cycle_balance = 0;
    z80_init(&msx->cpu);
    z80_reset(&msx->cpu);
    vdp_reset(&msx->vdp);
}

void msx_configure(MsxMachine *msx, MsxModel model, MsxRegion region,
                   int ram_kb) {
    int normalized_ram;

    if (!msx)
        return;
    msx->profile = msx_profile(model);
    psg_set_variant(&msx->psg, msx->profile->psg_variant);
    msx->region = region == MSX_REGION_NTSC
                ? MSX_REGION_NTSC : MSX_REGION_PAL;
    normalized_ram =
        msx_normalize_ram_kb(msx->profile->model, ram_kb);
    if (configure_ram_storage(msx, normalized_ram) != 0) {
        normalized_ram = msx_default_ram_kb(msx->profile->model);
        (void)configure_ram_storage(msx, normalized_ram);
        fprintf(stderr,
                "MSX: could not allocate requested mapper RAM; "
                "using %d KB\n",
                normalized_ram);
    }
    msx->ram_kb = normalized_ram;
    msx->frame_hz = msx->region == MSX_REGION_NTSC ? 60 : 50;
    vdp_set_type(&msx->vdp,
                 msx_model_is_msx2(msx->profile->model)
                 ? MSX_VDP_V9938 : MSX_VDP_TMS9918);
    msx_reset(msx);
}

void msx_init(MsxMachine *msx, MsxModel model, MsxRegion region, int ram_kb) {
    if (!msx)
        return;
    memset(msx, 0, sizeof(*msx));
    msx->ram = msx->internal_ram;
    msx->ram_capacity = sizeof(msx->internal_ram);
    for (unsigned i = 0; i < MSX_CARTRIDGE_SLOTS; ++i)
        msx_cartridge_init(&msx->cartridges[i]);
    cassette_init(&msx->cassette);
    megaflash_init(&msx->megaflash);
    sd_mapper_init(&msx->sd_mapper);
    sunrise_init(&msx->sunrise);
    wd2793_init(&msx->fdc);
    msx->megaflash_slot = -1;
    msx->sd_mapper_slot = -1;
    msx->sunrise_slot = -1;
    z80_init(&msx->cpu);
    vdp_init(&msx->vdp);
    psg_init(&msx->psg, PSG_VARIANT_AY8910);
    rtc_init(&msx->rtc);
    msx->bus.mem_read = bus_memory_read;
    msx->bus.mem_write = bus_memory_write;
    msx->bus.io_read = bus_io_read;
    msx->bus.io_write = bus_io_write;
    msx->bus.tick = bus_tick;
    msx->bus.ticked_in_step = &msx->bus_ticked_in_step;
    msx->bus.ctx = msx;
    msx_configure(msx, model, region, ram_kb);
}

void msx_destroy(MsxMachine *msx) {
    if (!msx)
        return;
    for (unsigned i = 0; i < MSX_CARTRIDGE_SLOTS; ++i)
        msx_cartridge_destroy(&msx->cartridges[i]);
    cassette_destroy(&msx->cassette);
    megaflash_destroy(&msx->megaflash);
    sd_mapper_destroy(&msx->sd_mapper);
    sunrise_destroy(&msx->sunrise);
    wd2793_destroy(&msx->fdc);
    msx->megaflash_slot = -1;
    msx->sd_mapper_slot = -1;
    msx->sunrise_slot = -1;
    if (msx->ram && msx->ram != msx->internal_ram)
        free(msx->ram);
    msx->ram = NULL;
    msx->ram_capacity = 0;
}

void msx_run_frame(MsxMachine *msx) {
    unsigned numerator;
    int frame_cycles;

    if (!msx)
        return;
    msx->audio_sample_count = 0;
    if (msx->paused)
        return;

    ++msx->frame;
    if (!msx_can_boot(msx))
        return;

    numerator = MSX_CPU_HZ + msx->cycle_fraction;
    frame_cycles = (int)(numerator / (unsigned)msx->frame_hz);
    msx->cycle_fraction = numerator % (unsigned)msx->frame_hz;
    vdp_begin_frame(
        &msx->vdp, (unsigned)frame_cycles,
        msx->region == MSX_REGION_NTSC
        ? MSX_NTSC_SCANLINES : MSX_PAL_SCANLINES);
    msx->cycle_balance += frame_cycles;
    while (msx->cycle_balance > 0) {
        msx->cpu.int_accepted = false;
        update_interrupt_line(msx);
        int consumed = z80_step(&msx->cpu, &msx->bus);
        if (consumed <= 0)
            consumed = 1;
        if (consumed > msx->bus_ticked_in_step)
            advance_machine(msx, consumed - msx->bus_ticked_in_step);
        msx->cycle_balance -= consumed;
        ++msx->instructions;
    }

    vdp_end_frame(&msx->vdp);
    update_interrupt_line(msx);
}

static unsigned selected_slot(const MsxMachine *msx, u16 address) {
    unsigned page = address >> 14;
    return (msx->primary_slot >> (page * 2)) & 3;
}

static bool slot_is_expanded(const MsxMachine *msx, unsigned primary) {
    if (msx->profile->expanded_slots && primary == 3)
        return true;
    return (primary == 1 || primary == 2) &&
           ((msx->sd_mapper_slot == (int)(primary - 1) &&
             sd_mapper_slot_expanded(&msx->sd_mapper)) ||
            (msx->megaflash_slot == (int)(primary - 1) &&
             megaflash_slot_expanded(&msx->megaflash)));
}

static unsigned selected_subslot(const MsxMachine *msx, unsigned primary,
                                 u16 address) {
    unsigned page = address >> 14;
    return (msx->secondary_slot[primary] >> (page * 2)) & 3;
}

static size_t mapper_ram_size(const MsxMachine *msx) {
    size_t size = (size_t)msx->ram_kb * 1024;

    if (size > msx->ram_capacity)
        size = msx->ram_capacity;
    return size;
}

static u8 mapper_segment_mask(const MsxMachine *msx) {
    size_t segments = mapper_ram_size(msx) / 0x4000;

    return segments ? (u8)(segments - 1) : 0;
}

static size_t mapper_address(const MsxMachine *msx, u16 address) {
    unsigned page = address >> 14;
    u8 segment = msx->mapper_segment[page] &
                 mapper_segment_mask(msx);

    return (size_t)segment * 0x4000 + (address & 0x3fff);
}

static u8 read_plain_ram(const MsxMachine *msx, u16 address) {
    size_t ram_size = (size_t)msx->ram_kb * 1024;
    size_t ram_base;

    if (ram_size > 0x10000)
        ram_size = 0x10000;
    ram_base = 0x10000 - ram_size;
    return address >= ram_base ? msx->ram[address] : 0xff;
}

static void write_plain_ram(MsxMachine *msx, u16 address, u8 value) {
    size_t ram_size = (size_t)msx->ram_kb * 1024;
    size_t ram_base;

    if (ram_size > 0x10000)
        ram_size = 0x10000;
    ram_base = 0x10000 - ram_size;
    if (address >= ram_base)
        msx->ram[address] = value;
}

u8 msx_memory_read(MsxMachine *msx, u16 address) {
    unsigned primary;

    if (!msx)
        return 0xff;
    primary = selected_slot(msx, address);
    if (address == 0xffff && slot_is_expanded(msx, primary)) {
        if ((primary == 1 || primary == 2) &&
            msx->sd_mapper_slot == (int)(primary - 1))
            return sd_mapper_secondary_read(&msx->sd_mapper);
        if ((primary == 1 || primary == 2) &&
            msx->megaflash_slot == (int)(primary - 1))
            return megaflash_secondary_read(&msx->megaflash);
        return msx->secondary_slot[primary] ^ 0xff;
    }

    switch (primary) {
        case 0:
            if (address < MSX_BIOS_SIZE && msx->bios_loaded)
                return msx->bios[address];
            if (address >= 0x8000 && address < 0xc000 && msx->logo_loaded)
                return msx->logo[address - 0x8000];
            break;
        case 1:
        case 2: {
            unsigned slot = primary - 1;

            if (msx->sd_mapper_slot == (int)slot)
                return sd_mapper_read(&msx->sd_mapper, address);
            if (msx->megaflash_slot == (int)slot)
                return megaflash_read(&msx->megaflash, address);
            if (msx->sunrise_slot == (int)slot)
                return sunrise_read(&msx->sunrise, address);
            return msx_cartridge_read(&msx->cartridges[slot], address);
        }
        case 3: {
            unsigned secondary;

            if (!slot_is_expanded(msx, primary)) {
                if (msx_has_memory_mapper(msx))
                    return msx->ram[mapper_address(msx, address)];
                return read_plain_ram(msx, address);
            }
            secondary = selected_subslot(msx, primary, address);
            if (secondary == 0 && msx->subrom_loaded)
                return msx->subrom[address & 0x3fff];
            if (secondary == 2 && msx_has_memory_mapper(msx))
                return msx->ram[mapper_address(msx, address)];
            if (secondary == 3 &&
                msx_floppy_supported(msx) &&
                wd2793_handles_address(address))
                return wd2793_read_memory(&msx->fdc, address);
            if (secondary == 3 && msx->disk_rom_loaded &&
                address >= 0x4000 && address < 0x8000)
                return msx->disk_rom[address - 0x4000];
            break;
        }
        default:
            break;
    }
    return 0xff;
}

void msx_memory_write(MsxMachine *msx, u16 address, u8 value) {
    unsigned primary;
    unsigned secondary;

    if (!msx)
        return;
    primary = selected_slot(msx, address);
    if (address == 0xffff && slot_is_expanded(msx, primary)) {
        if ((primary == 1 || primary == 2) &&
            msx->sd_mapper_slot == (int)(primary - 1))
            sd_mapper_secondary_write(&msx->sd_mapper, value);
        else if ((primary == 1 || primary == 2) &&
                 msx->megaflash_slot == (int)(primary - 1))
            megaflash_secondary_write(&msx->megaflash, value);
        else
            msx->secondary_slot[primary] = value;
        return;
    }
    if (primary == 1 || primary == 2) {
        unsigned slot = primary - 1;

        if (msx->sd_mapper_slot == (int)slot)
            sd_mapper_write(&msx->sd_mapper, address, value);
        else if (msx->megaflash_slot == (int)slot)
            megaflash_write(&msx->megaflash, address, value);
        else if (msx->sunrise_slot == (int)slot)
            sunrise_write(&msx->sunrise, address, value);
        else
            msx_cartridge_write(&msx->cartridges[slot],
                                address, value);
        return;
    }
    if (primary != 3)
        return;
    if (!slot_is_expanded(msx, primary)) {
        if (msx_has_memory_mapper(msx))
            msx->ram[mapper_address(msx, address)] = value;
        else
            write_plain_ram(msx, address, value);
        return;
    }
    secondary = selected_subslot(msx, primary, address);
    if (secondary == 2 && msx_has_memory_mapper(msx))
        msx->ram[mapper_address(msx, address)] = value;
    else if (secondary == 3 &&
             msx_floppy_supported(msx) &&
             wd2793_handles_address(address))
        wd2793_write_memory(&msx->fdc, address, value);
}

u8 msx_io_read(MsxMachine *msx, u16 port) {
    u8 low;

    if (!msx)
        return 0xff;
    low = (u8)port;
    switch (low) {
        case 0x98:
            return vdp_read_data(&msx->vdp);
        case 0x99: {
            u8 status = vdp_read_status(&msx->vdp);
            update_interrupt_line(msx);
            return status;
        }
        case 0x9a:
            if (msx->vdp.type == MSX_VDP_TMS9918)
                return vdp_read_data(&msx->vdp);
            break;
        case 0x9b:
            if (msx->vdp.type == MSX_VDP_TMS9918)
                return vdp_read_status(&msx->vdp);
            break;
        case 0xa2:
            if (msx->psg.selected == 14)
                return msx_joystick_read_psg(msx);
            return psg_read_data(&msx->psg);
        case 0xa8:
            return msx->primary_slot;
        case 0xa9:
            return msx_keyboard_read_row(msx, msx->ppi_port_c & 0x0f);
        case 0xaa:
            return msx->ppi_port_c;
        case 0xb4:
            break;
        case 0xb5:
            if (msx->profile->rtc)
                return rtc_read_data(&msx->rtc) | 0xf0;
            break;
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff:
        {
            u8 result = 0xff;

            if (msx_has_memory_mapper(msx))
                result &= msx->mapper_segment[low & 3] |
                          (u8)~mapper_segment_mask(msx);
            if (msx_sd_mapper_connected(msx))
                result &= sd_mapper_io_read(
                    &msx->sd_mapper, low & 3);
            if (msx_megaflash_connected(msx))
                result &= megaflash_mapper_io_read(
                    &msx->megaflash, low & 3);
            return result;
        }
        default:
            break;
    }
    return 0xff;
}

void msx_io_write(MsxMachine *msx, u16 port, u8 value) {
    u8 low;

    if (!msx)
        return;
    low = (u8)port;
    switch (low) {
        case 0x98:
            vdp_write_data(&msx->vdp, value);
            break;
        case 0x99:
            vdp_write_control(&msx->vdp, value);
            break;
        case 0x9a:
            if (msx->vdp.type == MSX_VDP_V9938)
                vdp_write_palette(&msx->vdp, value);
            else
                vdp_write_data(&msx->vdp, value);
            break;
        case 0x9b:
            if (msx->vdp.type == MSX_VDP_V9938)
                vdp_write_indirect(&msx->vdp, value);
            else
                vdp_write_control(&msx->vdp, value);
            break;
        case 0xa0:
            if (msx_megaflash_connected(msx))
                megaflash_psg_io_write(
                    &msx->megaflash, low, value);
            psg_select(&msx->psg, value);
            break;
        case 0xa1: {
            unsigned reg = msx->psg.selected;
            if (msx_megaflash_connected(msx))
                megaflash_psg_io_write(
                    &msx->megaflash, low, value);
            if (reg == 7)
                value = (value & 0x3f) | 0x80;
            psg_write_data(&msx->psg, value);
            if (reg == 15) {
                msx->kana_led = !(value & 0x80);
                for (unsigned port = 0;
                     port < MSX_JOYSTICK_PORTS; ++port)
                    msx_mouse_write_port(
                        msx, port,
                        msx_mouse_port_output(value, port));
            }
            break;
        }
        case 0xa8:
            msx->primary_slot = value;
            break;
        case 0xaa:
            msx->ppi_port_c = value;
            cassette_set_motor(
                &msx->cassette, !(value & 0x10), msx->cycles);
            cassette_set_output(
                &msx->cassette, (value & 0x20) != 0, msx->cycles);
            msx->caps_led = !(value & 0x40);
            break;
        case 0xab:
            if (!(value & 0x80)) {
                u8 mask = (u8)(1u << ((value >> 1) & 7));
                if (value & 1)
                    msx->ppi_port_c |= mask;
                else
                    msx->ppi_port_c &= (u8)~mask;
                cassette_set_motor(
                    &msx->cassette,
                    !(msx->ppi_port_c & 0x10), msx->cycles);
                cassette_set_output(
                    &msx->cassette,
                    (msx->ppi_port_c & 0x20) != 0,
                    msx->cycles);
                msx->caps_led = !(msx->ppi_port_c & 0x40);
            }
            break;
        case 0xb4:
            if (msx->profile->rtc)
                rtc_select(&msx->rtc, value);
            break;
        case 0xb5:
            if (msx->profile->rtc)
                rtc_write_data(&msx->rtc, value);
            break;
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff:
            if (msx_has_memory_mapper(msx))
                msx->mapper_segment[low & 3] =
                    value & mapper_segment_mask(msx);
            if (msx_sd_mapper_connected(msx))
                sd_mapper_io_write(
                    &msx->sd_mapper, low & 3, value);
            if (msx_megaflash_connected(msx))
                megaflash_mapper_io_write(
                    &msx->megaflash, low & 3, value);
            break;
        case 0x10:
        case 0x11:
            if (msx_megaflash_connected(msx))
                megaflash_psg_io_write(
                    &msx->megaflash, low, value);
            break;
        default:
            break;
    }
}

int msx_load_cassette(MsxMachine *msx, const char *path) {
    if (!msx)
        return -1;
    return cassette_mount_file(
        &msx->cassette, path, msx->cycles);
}

void msx_eject_cassette(MsxMachine *msx) {
    if (!msx)
        return;
    cassette_eject(&msx->cassette, msx->cycles);
}

void msx_rewind_cassette(MsxMachine *msx) {
    if (!msx)
        return;
    cassette_rewind(&msx->cassette, msx->cycles);
}

bool msx_cassette_mounted(const MsxMachine *msx) {
    return msx && cassette_is_mounted(&msx->cassette);
}

bool msx_cassette_rolling(MsxMachine *msx) {
    return msx &&
           cassette_is_rolling(&msx->cassette, msx->cycles);
}

bool msx_cassette_at_end(MsxMachine *msx) {
    return msx &&
           cassette_at_end(&msx->cassette, msx->cycles);
}

u64 msx_cassette_position_ms(MsxMachine *msx) {
    return msx
         ? cassette_position_ms(&msx->cassette, msx->cycles)
         : 0;
}

u64 msx_cassette_duration_ms(const MsxMachine *msx) {
    return msx
         ? cassette_duration_ms(&msx->cassette)
         : 0;
}

CassetteFileType msx_cassette_file_type(const MsxMachine *msx) {
    return msx
         ? cassette_file_type(&msx->cassette)
         : CASSETTE_FILE_UNKNOWN;
}

void msx_set_cassette_audible_monitor(MsxMachine *msx, bool enabled) {
    if (msx)
        msx->cassette_audible_monitor = enabled;
}

size_t msx_cassette_waveform_copy(MsxMachine *msx, s16 *samples,
                                  size_t capacity) {
    return msx
         ? cassette_waveform_copy(
               &msx->cassette, msx->cycles, samples, capacity)
         : 0;
}

int msx_install_bios(MsxMachine *msx, const u8 *data, size_t size) {
    if (!msx || !data || size != MSX_BIOS_SIZE)
        return -1;
    memcpy(msx->bios, data, size);
    msx->bios_loaded = true;
    msx_reset(msx);
    return 0;
}

int msx_install_logo(MsxMachine *msx, const u8 *data, size_t size) {
    if (!msx || !data || size != MSX_LOGO_SIZE)
        return -1;
    memcpy(msx->logo, data, size);
    msx->logo_loaded = true;
    return 0;
}

int msx_install_subrom(MsxMachine *msx, const u8 *data, size_t size) {
    if (!msx || !data || size != MSX_SUBROM_SIZE)
        return -1;
    memcpy(msx->subrom, data, size);
    msx->subrom_loaded = true;
    msx_reset(msx);
    return 0;
}

int msx_install_disk_rom(MsxMachine *msx, const u8 *data, size_t size) {
    if (!msx || !data || size != MSX_DISK_ROM_SIZE)
        return -1;
    memcpy(msx->disk_rom, data, size);
    msx->disk_rom_loaded = true;
    msx_reset(msx);
    return 0;
}

int msx_install_cartridge(MsxMachine *msx, const u8 *data, size_t size) {
    return msx_install_cartridge_slot(
        msx, 0, data, size, MSX_CART_MAPPER_AUTO);
}

int msx_install_cartridge_slot(MsxMachine *msx, unsigned slot,
                               const u8 *data, size_t size,
                               MsxCartridgeMapper mapper) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS)
        return -1;
    if (msx_cartridge_install(&msx->cartridges[slot], data, size,
                              mapper) != 0)
        return -1;
    msx_reset(msx);
    return 0;
}

typedef int (*RomInstaller)(MsxMachine *, const u8 *, size_t);

static int read_rom_file(const char *path, size_t maximum_size,
                         u8 **data_out, size_t *size_out) {
    FILE *file;
    u8 *data;
    long length;
    size_t got;
    int result;

    if (!path || !path[0] || !data_out || !size_out)
        return -1;
    file = fopen(path, "rb");
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    length = ftell(file);
    if (length <= 0 || (unsigned long)length > maximum_size ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    data = malloc((size_t)length);
    if (!data) {
        fclose(file);
        return -1;
    }
    got = fread(data, 1, (size_t)length, file);
    result = fclose(file);
    if (got != (size_t)length || result != 0) {
        free(data);
        return -1;
    }
    *data_out = data;
    *size_out = got;
    return 0;
}

static int load_rom(MsxMachine *msx, const char *path,
                    size_t maximum_size, RomInstaller install) {
    u8 *data;
    size_t size;
    int result;

    if (!msx ||
        read_rom_file(path, maximum_size, &data, &size) != 0)
        return -1;
    result = install(msx, data, size);
    free(data);
    return result;
}

int msx_load_bios(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_BIOS_SIZE, msx_install_bios);
}

int msx_load_logo(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_LOGO_SIZE, msx_install_logo);
}

int msx_load_subrom(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_SUBROM_SIZE, msx_install_subrom);
}

int msx_load_disk_rom(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_DISK_ROM_SIZE, msx_install_disk_rom);
}

int msx_load_cartridge(MsxMachine *msx, const char *path) {
    return msx_load_cartridge_slot(
        msx, 0, path, MSX_CART_MAPPER_AUTO);
}

int msx_load_cartridge_slot(MsxMachine *msx, unsigned slot,
                            const char *path, MsxCartridgeMapper mapper) {
    u8 *data;
    size_t size;

    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        !path || !path[0])
        return -1;
    if (read_rom_file(path, MSX_CART_MAX_SIZE, &data, &size) != 0)
        return -1;
    int result =
        msx_install_cartridge_slot(msx, slot, data, size, mapper);
    free(data);
    return result;
}

int msx_install_sunrise_ide(MsxMachine *msx, unsigned slot,
                            const u8 *data, size_t size) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        sunrise_install_rom(&msx->sunrise, data, size) != 0)
        return -1;
    msx_cartridge_eject(&msx->cartridges[slot]);
    msx->sunrise_slot = (int)slot;
    msx_reset(msx);
    return 0;
}

int msx_load_sunrise_ide(MsxMachine *msx, unsigned slot,
                         const char *path) {
    u8 *data;
    size_t size;
    int result;

    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        read_rom_file(path, MSX_SUNRISE_ROM_SIZE, &data, &size) != 0)
        return -1;
    result = msx_install_sunrise_ide(msx, slot, data, size);
    free(data);
    return result;
}

int msx_eject_sunrise_ide(MsxMachine *msx) {
    if (!msx)
        return -1;
    if (sunrise_eject_rom(&msx->sunrise) != 0)
        return -1;
    msx->sunrise_slot = -1;
    msx_reset(msx);
    return 0;
}

bool msx_sunrise_connected(const MsxMachine *msx) {
    return msx && msx->sunrise_slot >= 0 &&
           msx->sunrise_slot < (int)MSX_CARTRIDGE_SLOTS &&
           msx->sunrise.rom_loaded;
}

int msx_sunrise_slot(const MsxMachine *msx) {
    return msx_sunrise_connected(msx) ? msx->sunrise_slot : -1;
}

int msx_mount_sunrise_disk(MsxMachine *msx, const char *path) {
    return msx_mount_sunrise_disk_mode(
        msx, path, ATA_IMAGE_READ_ONLY);
}

int msx_mount_sunrise_disk_mode(MsxMachine *msx, const char *path,
                                AtaImageMode mode) {
    if (!msx_sunrise_connected(msx))
        return -1;
    return sunrise_mount_disk_mode(&msx->sunrise, path, mode);
}

int msx_flush_sunrise_disk(MsxMachine *msx) {
    return msx ? sunrise_flush_disk(&msx->sunrise) : -1;
}

int msx_eject_sunrise_disk(MsxMachine *msx) {
    return msx ? sunrise_eject_disk(&msx->sunrise) : -1;
}

bool msx_sunrise_disk_mounted(const MsxMachine *msx) {
    return msx_sunrise_connected(msx) &&
           sunrise_disk_mounted(&msx->sunrise);
}

bool msx_sunrise_disk_writable(const MsxMachine *msx) {
    return msx_sunrise_connected(msx) &&
           sunrise_disk_writable(&msx->sunrise);
}

bool msx_sunrise_disk_dirty(const MsxMachine *msx) {
    return msx_sunrise_connected(msx) &&
           sunrise_disk_dirty(&msx->sunrise);
}

bool msx_sunrise_disk_has_error(const MsxMachine *msx) {
    return msx && sunrise_disk_has_error(&msx->sunrise);
}

const char *msx_sunrise_disk_error(const MsxMachine *msx) {
    return msx ? sunrise_disk_error(&msx->sunrise) : "";
}

bool msx_sunrise_take_activity(MsxMachine *msx) {
    return msx_sunrise_connected(msx) &&
           sunrise_take_activity(&msx->sunrise);
}

int msx_install_sd_mapper(MsxMachine *msx, unsigned slot,
                          const u8 *data, size_t size) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        sd_mapper_install_rom(&msx->sd_mapper, data, size) != 0)
        return -1;
    msx_cartridge_eject(&msx->cartridges[slot]);
    msx->sd_mapper_slot = (int)slot;
    msx_reset(msx);
    return 0;
}

int msx_load_sd_mapper(MsxMachine *msx, unsigned slot,
                       const char *path) {
    u8 *data;
    size_t size;
    int result;

    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        read_rom_file(path, MSX_SD_MAPPER_ROM_SIZE,
                      &data, &size) != 0)
        return -1;
    result = msx_install_sd_mapper(msx, slot, data, size);
    free(data);
    return result;
}

int msx_eject_sd_mapper(MsxMachine *msx) {
    if (!msx)
        return -1;
    if (sd_mapper_eject_rom(&msx->sd_mapper) != 0)
        return -1;
    msx->sd_mapper_slot = -1;
    msx_reset(msx);
    return 0;
}

bool msx_sd_mapper_connected(const MsxMachine *msx) {
    return msx && msx->sd_mapper_slot >= 0 &&
           msx->sd_mapper_slot < (int)MSX_CARTRIDGE_SLOTS &&
           msx->sd_mapper.rom_loaded;
}

int msx_sd_mapper_slot(const MsxMachine *msx) {
    return msx_sd_mapper_connected(msx)
         ? msx->sd_mapper_slot : -1;
}

void msx_sd_mapper_set_ram_enabled(MsxMachine *msx, bool enabled) {
    if (msx)
        sd_mapper_set_mapper_enabled(&msx->sd_mapper, enabled);
}

void msx_sd_mapper_set_alternate_driver(MsxMachine *msx,
                                        bool alternate) {
    if (msx)
        sd_mapper_set_alternate_driver(
            &msx->sd_mapper, alternate);
}

int msx_mount_sd_card(MsxMachine *msx, unsigned card,
                      const char *path, SdImageMode mode) {
    if (!msx_sd_mapper_connected(msx))
        return -1;
    return sd_mapper_mount_card(
        &msx->sd_mapper, card, path, mode);
}

int msx_flush_sd_card(MsxMachine *msx, unsigned card) {
    return msx ? sd_mapper_flush_card(
        &msx->sd_mapper, card) : -1;
}

int msx_eject_sd_card(MsxMachine *msx, unsigned card) {
    return msx ? sd_mapper_eject_card(
        &msx->sd_mapper, card) : -1;
}

bool msx_sd_card_mounted(const MsxMachine *msx, unsigned card) {
    return msx_sd_mapper_connected(msx) &&
           sd_mapper_card_mounted(&msx->sd_mapper, card);
}

bool msx_sd_card_writable(const MsxMachine *msx, unsigned card) {
    return msx_sd_mapper_connected(msx) &&
           sd_mapper_card_writable(&msx->sd_mapper, card);
}

bool msx_sd_card_dirty(const MsxMachine *msx, unsigned card) {
    return msx_sd_mapper_connected(msx) &&
           sd_mapper_card_dirty(&msx->sd_mapper, card);
}

bool msx_sd_card_has_error(const MsxMachine *msx, unsigned card) {
    return msx &&
           sd_mapper_card_has_error(&msx->sd_mapper, card);
}

const char *msx_sd_card_error(const MsxMachine *msx, unsigned card) {
    return msx
         ? sd_mapper_card_error(&msx->sd_mapper, card) : "";
}

bool msx_sd_card_take_activity(MsxMachine *msx, unsigned card) {
    return msx_sd_mapper_connected(msx) &&
           sd_mapper_take_activity(&msx->sd_mapper, card);
}

int msx_install_megaflash(MsxMachine *msx, unsigned slot,
                          const u8 *data, size_t size) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        megaflash_install(&msx->megaflash, data, size) != 0)
        return -1;
    msx_cartridge_eject(&msx->cartridges[slot]);
    msx->megaflash_slot = (int)slot;
    msx_reset(msx);
    return 0;
}

int msx_load_megaflash(MsxMachine *msx, unsigned slot,
                       const char *path) {
    u8 *data;
    size_t size;
    int result;

    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        read_rom_file(path, MSX_MEGAFLASH_FLASH_SIZE,
                      &data, &size) != 0)
        return -1;
    result = msx_install_megaflash(msx, slot, data, size);
    free(data);
    return result;
}

int msx_load_megaflash_persistent(MsxMachine *msx, unsigned slot,
                                  const char *initial_path,
                                  const char *state_path) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        megaflash_load_persistent(
            &msx->megaflash, initial_path, state_path) != 0)
        return -1;
    msx_cartridge_eject(&msx->cartridges[slot]);
    msx->megaflash_slot = (int)slot;
    msx_reset(msx);
    return 0;
}

int msx_flush_megaflash(MsxMachine *msx) {
    return msx ? megaflash_flush_flash(&msx->megaflash) : -1;
}

bool msx_megaflash_flash_dirty(const MsxMachine *msx) {
    return msx &&
           megaflash_flash_dirty(&msx->megaflash);
}

bool msx_megaflash_flash_has_error(const MsxMachine *msx) {
    return msx &&
           megaflash_flash_has_error(&msx->megaflash);
}

const char *msx_megaflash_flash_error(const MsxMachine *msx) {
    return msx
         ? megaflash_flash_error(&msx->megaflash) : "";
}

int msx_eject_megaflash(MsxMachine *msx) {
    if (!msx || megaflash_eject(&msx->megaflash) != 0)
        return -1;
    msx->megaflash_slot = -1;
    msx_reset(msx);
    return 0;
}

bool msx_megaflash_connected(const MsxMachine *msx) {
    return msx && msx->megaflash_slot >= 0 &&
           msx->megaflash_slot < (int)MSX_CARTRIDGE_SLOTS &&
           msx->megaflash.loaded;
}

int msx_megaflash_slot(const MsxMachine *msx) {
    return msx_megaflash_connected(msx)
         ? msx->megaflash_slot : -1;
}

int msx_mount_megaflash_card(MsxMachine *msx, unsigned card,
                             const char *path, SdImageMode mode) {
    return msx_megaflash_connected(msx)
         ? megaflash_mount_card(
               &msx->megaflash, card, path, mode) : -1;
}

int msx_flush_megaflash_card(MsxMachine *msx, unsigned card) {
    return msx ? megaflash_flush_card(
        &msx->megaflash, card) : -1;
}

int msx_eject_megaflash_card(MsxMachine *msx, unsigned card) {
    return msx ? megaflash_eject_card(
        &msx->megaflash, card) : -1;
}

bool msx_megaflash_card_mounted(const MsxMachine *msx, unsigned card) {
    return msx_megaflash_connected(msx) &&
           megaflash_card_mounted(&msx->megaflash, card);
}

bool msx_megaflash_card_writable(const MsxMachine *msx,
                                 unsigned card) {
    return msx_megaflash_connected(msx) &&
           megaflash_card_writable(&msx->megaflash, card);
}

bool msx_megaflash_card_dirty(const MsxMachine *msx, unsigned card) {
    return msx_megaflash_connected(msx) &&
           megaflash_card_dirty(&msx->megaflash, card);
}

bool msx_megaflash_card_has_error(const MsxMachine *msx,
                                  unsigned card) {
    return msx &&
           megaflash_card_has_error(&msx->megaflash, card);
}

const char *msx_megaflash_card_error(const MsxMachine *msx,
                                     unsigned card) {
    return msx
         ? megaflash_card_error(&msx->megaflash, card) : "";
}

bool msx_megaflash_take_activity(MsxMachine *msx, unsigned card) {
    return msx_megaflash_connected(msx) &&
           megaflash_take_activity(&msx->megaflash, card);
}

int msx_flush_rtc_persistence(MsxMachine *msx, u64 host_seconds) {
    if (!msx || !msx->rtc_persistence_path[0] ||
        !rtc_dirty(&msx->rtc))
        return 0;
    if (rtc_save_persistence(
            &msx->rtc, msx->rtc_persistence_path,
            host_seconds, msx->rtc_persistence_error,
            sizeof(msx->rtc_persistence_error)) != 0)
        return -1;
    msx->rtc_persistence_error[0] = '\0';
    return 0;
}

int msx_set_rtc_persistence(MsxMachine *msx, const char *path,
                            u64 host_seconds) {
    MsxRtc candidate;
    int result;

    if (!msx)
        return -1;
    if (!path)
        path = "";
    if (strcmp(msx->rtc_persistence_path, path) == 0)
        return 0;
    if (msx_flush_rtc_persistence(msx, host_seconds) != 0)
        return -1;
    if (!path[0] || !msx->profile || !msx->profile->rtc) {
        msx->rtc_persistence_path[0] = '\0';
        msx->rtc_persistence_error[0] = '\0';
        return 0;
    }
    if (strlen(path) >= sizeof(msx->rtc_persistence_path)) {
        snprintf(msx->rtc_persistence_error,
                 sizeof(msx->rtc_persistence_error),
                 "RTC persistence path is too long");
        return -1;
    }

    rtc_init_at(&candidate, host_seconds);
    result = rtc_load_persistence(
        &candidate, path, host_seconds,
        msx->rtc_persistence_error,
        sizeof(msx->rtc_persistence_error));
    if (result != 0)
        candidate.dirty = true;
    msx->rtc = candidate;
    snprintf(msx->rtc_persistence_path,
             sizeof(msx->rtc_persistence_path), "%s", path);
    return result < 0 ? -1 : 0;
}

bool msx_rtc_persistence_active(const MsxMachine *msx) {
    return msx && msx->rtc_persistence_path[0];
}

bool msx_rtc_persistence_dirty(const MsxMachine *msx) {
    return msx && rtc_dirty(&msx->rtc);
}

bool msx_rtc_persistence_has_error(const MsxMachine *msx) {
    return msx && msx->rtc_persistence_error[0];
}

const char *msx_rtc_persistence_error(const MsxMachine *msx) {
    return msx ? msx->rtc_persistence_error : "";
}

const char *msx_rtc_persistence_path(const MsxMachine *msx) {
    return msx ? msx->rtc_persistence_path : "";
}

bool msx_floppy_supported(const MsxMachine *msx) {
    return msx && msx->profile &&
           msx->profile->model == MSX_MODEL_PHILIPS_NMS8250;
}

int msx_mount_drive_a(MsxMachine *msx, const char *path,
                      FloppyImageMode mode) {
    if (!msx || !msx_floppy_supported(msx))
        return -1;
    return wd2793_mount_drive_a(&msx->fdc, path, mode);
}

int msx_flush_drive_a(MsxMachine *msx) {
    return msx ? wd2793_flush_drive_a(&msx->fdc) : -1;
}

int msx_eject_drive_a(MsxMachine *msx) {
    return msx ? wd2793_eject_drive_a(&msx->fdc) : -1;
}

bool msx_drive_a_mounted(const MsxMachine *msx) {
    return msx && wd2793_drive_a_mounted(&msx->fdc);
}

bool msx_drive_a_writable(const MsxMachine *msx) {
    return msx && wd2793_drive_a_writable(&msx->fdc);
}

bool msx_drive_a_dirty(const MsxMachine *msx) {
    return msx && wd2793_drive_a_dirty(&msx->fdc);
}

bool msx_drive_a_has_error(const MsxMachine *msx) {
    return msx && wd2793_drive_a_has_error(&msx->fdc);
}

const char *msx_drive_a_error(const MsxMachine *msx) {
    return msx ? wd2793_drive_a_error(&msx->fdc) : "";
}

bool msx_drive_a_take_activity(MsxMachine *msx) {
    return msx && wd2793_take_drive_a_activity(&msx->fdc);
}

int msx_mount_drive_b(MsxMachine *msx, const char *path,
                      FloppyImageMode mode) {
    if (!msx || !msx_floppy_supported(msx))
        return -1;
    return wd2793_mount_drive_b(&msx->fdc, path, mode);
}

int msx_flush_drive_b(MsxMachine *msx) {
    return msx ? wd2793_flush_drive_b(&msx->fdc) : -1;
}

int msx_eject_drive_b(MsxMachine *msx) {
    return msx ? wd2793_eject_drive_b(&msx->fdc) : -1;
}

bool msx_drive_b_mounted(const MsxMachine *msx) {
    return msx && wd2793_drive_b_mounted(&msx->fdc);
}

bool msx_drive_b_writable(const MsxMachine *msx) {
    return msx && wd2793_drive_b_writable(&msx->fdc);
}

bool msx_drive_b_dirty(const MsxMachine *msx) {
    return msx && wd2793_drive_b_dirty(&msx->fdc);
}

bool msx_drive_b_has_error(const MsxMachine *msx) {
    return msx && wd2793_drive_b_has_error(&msx->fdc);
}

const char *msx_drive_b_error(const MsxMachine *msx) {
    return msx ? wd2793_drive_b_error(&msx->fdc) : "";
}

bool msx_drive_b_take_activity(MsxMachine *msx) {
    return msx && wd2793_take_drive_b_activity(&msx->fdc);
}

int msx_set_cartridge_mapper(MsxMachine *msx, unsigned slot,
                             MsxCartridgeMapper mapper) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS ||
        msx_cartridge_set_mapper(&msx->cartridges[slot], mapper) != 0)
        return -1;
    msx_reset(msx);
    return 0;
}

void msx_eject_cartridge(MsxMachine *msx, unsigned slot) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS)
        return;
    msx_cartridge_eject(&msx->cartridges[slot]);
    msx_reset(msx);
}

const MsxCartridge *msx_get_cartridge(const MsxMachine *msx, unsigned slot) {
    if (!msx || slot >= MSX_CARTRIDGE_SLOTS)
        return NULL;
    return &msx->cartridges[slot];
}

static int read_firmware_component(const char *path, size_t required_size,
                                   u8 **data_out) {
    size_t size;

    if (read_rom_file(path, required_size, data_out, &size) != 0)
        return -1;
    if (size != required_size) {
        free(*data_out);
        *data_out = NULL;
        return -1;
    }
    return 0;
}

int msx_load_firmware_set(MsxMachine *msx, const char *bios_path,
                          const char *logo_path,
                          const char *subrom_path,
                          const char *disk_rom_path) {
    u8 *bios = NULL;
    u8 *logo = NULL;
    u8 *subrom = NULL;
    u8 *disk_rom = NULL;
    int result = -1;

    if (!msx ||
        read_firmware_component(bios_path, MSX_BIOS_SIZE, &bios) != 0)
        goto done;
    if (logo_path && logo_path[0] &&
        read_firmware_component(
            logo_path, MSX_LOGO_SIZE, &logo) != 0)
        goto done;
    if (subrom_path && subrom_path[0] &&
        read_firmware_component(
            subrom_path, MSX_SUBROM_SIZE, &subrom) != 0)
        goto done;
    if (disk_rom_path && disk_rom_path[0] &&
        read_firmware_component(
            disk_rom_path, MSX_DISK_ROM_SIZE, &disk_rom) != 0)
        goto done;

    memcpy(msx->bios, bios, MSX_BIOS_SIZE);
    msx->bios_loaded = true;
    memset(msx->logo, 0xff, sizeof(msx->logo));
    msx->logo_loaded = logo != NULL;
    if (logo)
        memcpy(msx->logo, logo, MSX_LOGO_SIZE);
    memset(msx->subrom, 0xff, sizeof(msx->subrom));
    msx->subrom_loaded = subrom != NULL;
    if (subrom)
        memcpy(msx->subrom, subrom, MSX_SUBROM_SIZE);
    memset(msx->disk_rom, 0xff, sizeof(msx->disk_rom));
    msx->disk_rom_loaded = disk_rom != NULL;
    if (disk_rom)
        memcpy(msx->disk_rom, disk_rom, MSX_DISK_ROM_SIZE);
    msx_reset(msx);
    result = 0;

done:
    free(bios);
    free(logo);
    free(subrom);
    free(disk_rom);
    return result;
}

void msx_eject_firmware(MsxMachine *msx) {
    if (!msx)
        return;
    memset(msx->bios, 0xff, sizeof(msx->bios));
    memset(msx->logo, 0xff, sizeof(msx->logo));
    memset(msx->subrom, 0xff, sizeof(msx->subrom));
    memset(msx->disk_rom, 0xff, sizeof(msx->disk_rom));
    msx->bios_loaded = false;
    msx->logo_loaded = false;
    msx->subrom_loaded = false;
    msx->disk_rom_loaded = false;
    msx_reset(msx);
}

bool msx_can_boot(const MsxMachine *msx) {
    return msx && msx->bios_loaded;
}
