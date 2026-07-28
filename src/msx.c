#include "msx.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static const MsxProfile profiles[MSX_MODEL_COUNT] = {
    [MSX_MODEL_GENERIC_MSX1] = {
        .model = MSX_MODEL_GENERIC_MSX1,
        .name = "Generic MSX1",
        .default_ram_kb = 64,
        .vram_kb = 16,
        .expanded_slots = false,
        .memory_mapper = false,
        .rtc = false,
    },
    [MSX_MODEL_GENERIC_MSX2] = {
        .model = MSX_MODEL_GENERIC_MSX2,
        .name = "Generic MSX2",
        .default_ram_kb = 128,
        .vram_kb = 128,
        .expanded_slots = true,
        .memory_mapper = true,
        .rtc = true,
    },
};

static const int msx1_ram_sizes[] = { 16, 32, 64 };
static const int msx2_ram_sizes[] = { 64, 128, 256, 512, 1024, 2048, 4096 };

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

const MsxProfile *msx_profile(MsxModel model) {
    if ((unsigned)model >= MSX_MODEL_COUNT)
        model = MSX_MODEL_GENERIC_MSX1;
    return &profiles[model];
}

const char *msx_model_name(MsxModel model) {
    return msx_profile(model)->name;
}

const char *msx_region_name(MsxRegion region) {
    return region == MSX_REGION_NTSC ? "NTSC 60 Hz" : "PAL 50 Hz";
}

const char *msx_vdp_name(const MsxMachine *msx) {
    if (msx && msx->profile->model == MSX_MODEL_GENERIC_MSX2)
        return "V9938";
    return msx && msx->region == MSX_REGION_NTSC
         ? "TMS9918A" : "TMS9929A";
}

int msx_default_ram_kb(MsxModel model) {
    return msx_profile(model)->default_ram_kb;
}

static const int *ram_sizes(MsxModel model, size_t *count) {
    if (model == MSX_MODEL_GENERIC_MSX2) {
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

void msx_reset(MsxMachine *msx) {
    if (!msx)
        return;
    msx->frame = 0;
    msx->primary_slot = 0;
    memset(msx->secondary_slot, 0, sizeof(msx->secondary_slot));
    memset(msx->mapper_segment, 0, sizeof(msx->mapper_segment));
    msx->paused = false;
    msx->caps_led = false;
    msx->kana_led = false;
    msx->ppi_port_c = 0xff;
    msx_keyboard_clear(msx);
    msx->psg_register = 0;
    memset(msx->psg, 0, sizeof(msx->psg));
    memset(msx->ram, 0, sizeof(msx->ram));
    msx->cycles = 0;
    msx->instructions = 0;
    msx->cycle_fraction = 0;
    msx->cycle_balance = 0;
    z80_init(&msx->cpu);
    z80_reset(&msx->cpu);
    vdp_reset(&msx->vdp);
}

void msx_configure(MsxMachine *msx, MsxModel model, MsxRegion region,
                   int ram_kb) {
    if (!msx)
        return;
    msx->profile = msx_profile(model);
    msx->region = region == MSX_REGION_NTSC
                ? MSX_REGION_NTSC : MSX_REGION_PAL;
    msx->ram_kb = msx_normalize_ram_kb(msx->profile->model, ram_kb);
    msx->frame_hz = msx->region == MSX_REGION_NTSC ? 60 : 50;
    msx_reset(msx);
}

void msx_init(MsxMachine *msx, MsxModel model, MsxRegion region, int ram_kb) {
    if (!msx)
        return;
    memset(msx, 0, sizeof(*msx));
    z80_init(&msx->cpu);
    vdp_init(&msx->vdp);
    msx->bus.mem_read = bus_memory_read;
    msx->bus.mem_write = bus_memory_write;
    msx->bus.io_read = bus_io_read;
    msx->bus.io_write = bus_io_write;
    msx->bus.ctx = msx;
    msx_configure(msx, model, region, ram_kb);
}

void msx_run_frame(MsxMachine *msx) {
    unsigned numerator;
    int frame_cycles;

    if (!msx || msx->paused)
        return;

    ++msx->frame;
    if (!msx_can_boot(msx))
        return;

    numerator = MSX_CPU_HZ + msx->cycle_fraction;
    frame_cycles = (int)(numerator / (unsigned)msx->frame_hz);
    msx->cycle_fraction = numerator % (unsigned)msx->frame_hz;
    msx->cycle_balance += frame_cycles;
    while (msx->cycle_balance > 0) {
        msx->cpu.int_accepted = false;
        int consumed = z80_step(&msx->cpu, &msx->bus);
        if (consumed <= 0)
            consumed = 1;
        msx->cycle_balance -= consumed;
        msx->cycles += (unsigned)consumed;
        ++msx->instructions;
    }

    vdp_end_frame(&msx->vdp);
    if (msx->vdp.irq)
        z80_interrupt(&msx->cpu);
}

static unsigned selected_slot(const MsxMachine *msx, u16 address) {
    unsigned page = address >> 14;
    return (msx->primary_slot >> (page * 2)) & 3;
}

u8 msx_memory_read(MsxMachine *msx, u16 address) {
    size_t offset;

    if (!msx)
        return 0xff;
    switch (selected_slot(msx, address)) {
        case 0:
            if (address < MSX_BIOS_SIZE && msx->bios_loaded)
                return msx->bios[address];
            if (address >= 0x8000 && address < 0xc000 && msx->logo_loaded)
                return msx->logo[address - 0x8000];
            break;
        case 1:
            if (msx->cartridge_loaded &&
                address >= msx->cartridge_base) {
                offset = (size_t)(address - msx->cartridge_base);
                if (offset < msx->cartridge_size)
                    return msx->cartridge[offset];
            }
            break;
        case 3: {
            size_t ram_size = (size_t)msx->ram_kb * 1024;
            size_t ram_base;
            if (ram_size > MSX_RAM_MAX_SIZE)
                ram_size = MSX_RAM_MAX_SIZE;
            ram_base = MSX_RAM_MAX_SIZE - ram_size;
            if (address >= ram_base)
                return msx->ram[address];
            break;
        }
        default:
            break;
    }
    return 0xff;
}

void msx_memory_write(MsxMachine *msx, u16 address, u8 value) {
    size_t ram_size;
    size_t ram_base;

    if (!msx || selected_slot(msx, address) != 3)
        return;
    ram_size = (size_t)msx->ram_kb * 1024;
    if (ram_size > MSX_RAM_MAX_SIZE)
        ram_size = MSX_RAM_MAX_SIZE;
    ram_base = MSX_RAM_MAX_SIZE - ram_size;
    if (address >= ram_base)
        msx->ram[address] = value;
}

u8 msx_io_read(MsxMachine *msx, u16 port) {
    u8 low;

    if (!msx)
        return 0xff;
    low = (u8)port;
    switch (low) {
        case 0x98:
            return vdp_read_data(&msx->vdp);
        case 0x99:
            return vdp_read_status(&msx->vdp);
        case 0xa2:
            if (msx->psg_register == 14 || msx->psg_register == 15)
                return 0xff;
            return msx->psg[msx->psg_register & 0x0f];
        case 0xa8:
            return msx->primary_slot;
        case 0xa9:
            return msx_keyboard_read_row(msx, msx->ppi_port_c & 0x0f);
        case 0xaa:
            return msx->ppi_port_c;
        default:
            return 0xff;
    }
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
        case 0xa0:
            msx->psg_register = value & 0x0f;
            break;
        case 0xa1:
            msx->psg[msx->psg_register & 0x0f] = value;
            break;
        case 0xa8:
            msx->primary_slot = value;
            break;
        case 0xaa:
            msx->ppi_port_c = value;
            msx->caps_led = !(value & 0x40);
            break;
        case 0xab:
            if (!(value & 0x80)) {
                u8 mask = (u8)(1u << ((value >> 1) & 7));
                if (value & 1)
                    msx->ppi_port_c |= mask;
                else
                    msx->ppi_port_c &= (u8)~mask;
                msx->caps_led = !(msx->ppi_port_c & 0x40);
            }
            break;
        default:
            break;
    }
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

static u16 cartridge_base(const u8 *data, size_t size) {
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

int msx_install_cartridge(MsxMachine *msx, const u8 *data, size_t size) {
    if (!msx || !data || size == 0 || size > MSX_CART_MAX_SIZE)
        return -1;
    memset(msx->cartridge, 0xff, sizeof(msx->cartridge));
    memcpy(msx->cartridge, data, size);
    msx->cartridge_size = size;
    msx->cartridge_base = cartridge_base(data, size);
    msx->cartridge_loaded = true;
    msx_reset(msx);
    return 0;
}

typedef int (*RomInstaller)(MsxMachine *, const u8 *, size_t);

static int load_rom(MsxMachine *msx, const char *path,
                    size_t maximum_size, RomInstaller install) {
    FILE *file;
    u8 *data;
    long length;
    size_t got;
    int result;

    if (!msx || !path || !path[0])
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
    result = install(msx, data, got);
    free(data);
    return result;
}

int msx_load_bios(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_BIOS_SIZE, msx_install_bios);
}

int msx_load_logo(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_LOGO_SIZE, msx_install_logo);
}

int msx_load_cartridge(MsxMachine *msx, const char *path) {
    return load_rom(msx, path, MSX_CART_MAX_SIZE, msx_install_cartridge);
}

bool msx_can_boot(const MsxMachine *msx) {
    return msx && msx->bios_loaded;
}
