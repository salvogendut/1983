#include "scc.h"

#include <assert.h>
#include <stdio.h>

static void test_compatible_register_map(void) {
    Scc scc;

    scc_init(&scc);
    scc_write(&scc, 0x00, 0x21);
    scc_write(&scc, 0x60, 0x43);
    assert(scc_read(&scc, 0x00) == 0x21);
    assert(scc_read(&scc, 0x60) == 0x43);
    assert(scc_read(&scc, 0xa0) == 0x43);
    scc_write(&scc, 0xa0, 0x66);
    assert(scc_read(&scc, 0xa0) == 0x43);
    scc_write(&scc, 0x80, 0x34);
    scc_write(&scc, 0x81, 0x02);
    scc_write(&scc, 0x8a, 0x0f);
    scc_write(&scc, 0x8f, 0x01);
    assert(scc.original_period[0] == 0x234);
    assert(scc.volume[0] == 15);
    assert(scc.enabled == 1);
}

static void test_plus_wave_and_audio(void) {
    Scc scc;
    s16 samples[128] = {0};
    bool nonzero = false;

    scc_init(&scc);
    scc_set_mode(&scc, SCC_MODE_PLUS);
    for (unsigned i = 0; i < SCC_WAVE_SIZE; ++i)
        scc_write(&scc, (u8)(0x80 + i),
                  i < 16 ? 0x60 : 0xa0);
    scc_write(&scc, 0xa8, 0x40);
    scc_write(&scc, 0xa9, 0x00);
    scc_write(&scc, 0xae, 0x0f);
    scc_write(&scc, 0xaf, 0x10);
    assert(scc_read(&scc, 0x80) == 0x60);
    assert(scc_read(&scc, 0x90) == 0xa0);
    scc_render(&scc, samples, 128, 3579545, 44100);
    for (unsigned i = 0; i < 128; ++i)
        nonzero |= samples[i] != 0;
    assert(nonzero);
    scc_reset(&scc);
    assert(scc.enabled == 0);
}

int main(void) {
    test_compatible_register_map();
    test_plus_wave_and_audio();
    puts("SCC tests passed");
    return 0;
}
