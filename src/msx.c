#include "msx.h"

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
    msx_configure(msx, model, region, ram_kb);
}

void msx_run_frame(MsxMachine *msx) {
    if (msx && !msx->paused)
        ++msx->frame;
}
