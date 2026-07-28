#include "msx.h"

#include <assert.h>
#include <string.h>

int main(void) {
    MsxMachine msx;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    assert(strcmp(msx.profile->name, "Generic MSX1") == 0);
    assert(strcmp(msx_vdp_name(&msx), "TMS9929A") == 0);
    assert(msx.profile->vram_kb == 16);
    assert(!msx.profile->expanded_slots);
    assert(!msx.profile->memory_mapper);
    assert(!msx.profile->rtc);
    assert(msx.frame_hz == 50);

    msx_run_frame(&msx);
    assert(msx.frame == 1);
    msx.paused = true;
    msx_run_frame(&msx);
    assert(msx.frame == 1);

    msx_configure(&msx, MSX_MODEL_GENERIC_MSX2, MSX_REGION_NTSC, 200);
    assert(strcmp(msx_vdp_name(&msx), "V9938") == 0);
    assert(msx.ram_kb == 128);
    assert(msx.profile->vram_kb == 128);
    assert(msx.profile->expanded_slots);
    assert(msx.profile->memory_mapper);
    assert(msx.profile->rtc);
    assert(msx.frame_hz == 60);
    assert(msx.frame == 0);

    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX1, 64, 1) == 16);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX1, 16, -1) == 64);
    assert(msx_next_ram_kb(MSX_MODEL_GENERIC_MSX2, 128, 1) == 256);
    return 0;
}
