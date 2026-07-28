#pragma once

#include <stdbool.h>

#include "types.h"

#define MSX_CPU_HZ 3579545u

typedef enum {
    MSX_MODEL_GENERIC_MSX1 = 0,
    MSX_MODEL_GENERIC_MSX2,
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
} MsxMachine;

const MsxProfile *msx_profile(MsxModel model);
const char *msx_model_name(MsxModel model);
const char *msx_region_name(MsxRegion region);
const char *msx_vdp_name(const MsxMachine *msx);

int  msx_default_ram_kb(MsxModel model);
int  msx_normalize_ram_kb(MsxModel model, int ram_kb);
int  msx_next_ram_kb(MsxModel model, int ram_kb, int direction);

void msx_init(MsxMachine *msx, MsxModel model, MsxRegion region, int ram_kb);
void msx_configure(MsxMachine *msx, MsxModel model, MsxRegion region,
                   int ram_kb);
void msx_reset(MsxMachine *msx);
void msx_run_frame(MsxMachine *msx);
