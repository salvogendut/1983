#include "psg.h"

#include <assert.h>
#include <string.h>

static void test_register_latch_and_variant_readback(void) {
    Psg psg;

    psg_init(&psg, PSG_VARIANT_AY8910);
    psg_select(&psg, 0x21);
    assert(psg.selected == 1);
    psg_write_data(&psg, 0xff);
    assert(psg.registers[1] == 0xff);
    assert(psg_read_data(&psg) == 0x0f);

    psg_write_register(&psg, 6, 0xff);
    assert(psg_read_register(&psg, 6) == 0x1f);
    psg_write_register(&psg, 8, 0xff);
    assert(psg_read_register(&psg, 8) == 0x1f);
    psg_write_register(&psg, 13, 0xff);
    assert(psg_read_register(&psg, 13) == 0x0f);

    psg_set_variant(&psg, PSG_VARIANT_YM2149);
    assert(psg_read_register(&psg, 1) == 0xff);
    assert(psg_read_register(&psg, 6) == 0xff);
    assert(psg_read_register(&psg, 8) == 0xff);
    assert(psg_read_register(&psg, 13) == 0xff);
    assert(psg_read_register(&psg, PSG_REGISTER_COUNT) == 0xff);
}

static void test_tone_and_noise_periods(void) {
    Psg psg;

    psg_init(&psg, PSG_VARIANT_AY8910);
    psg_write_register(&psg, 0, 2);
    psg_advance_ticks(&psg, 15);
    assert(!psg.tone_output[0]);
    psg_advance_ticks(&psg, 1);
    assert(psg.tone_output[0]);
    psg_advance_ticks(&psg, 16);
    assert(!psg.tone_output[0]);

    psg_reset(&psg);
    psg_write_register(&psg, 6, 1);
    psg_advance_ticks(&psg, 15);
    assert(psg.noise_lfsr == 1);
    psg_advance_ticks(&psg, 1);
    assert(psg.noise_lfsr == 0x10000);
}

static void test_envelope_period_and_variants(void) {
    Psg psg;

    psg_init(&psg, PSG_VARIANT_YM2149);
    psg_write_register(&psg, 11, 1);
    psg_write_register(&psg, 12, 0);
    psg_write_register(&psg, 13, 0x0c);
    assert(psg_envelope_level(&psg) == 0);
    psg_advance_ticks(&psg, 7);
    assert(psg_envelope_level(&psg) == 0);
    psg_advance_ticks(&psg, 1);
    assert(psg_envelope_level(&psg) == 1);

    /* AY envelopes have 16 analogue levels and repeat each of the
     * YM2149's 32 logical steps. */
    psg_set_variant(&psg, PSG_VARIANT_AY8910);
    assert(psg_envelope_level(&psg) == 0);
    psg_set_variant(&psg, PSG_VARIANT_YM2149);
    assert(psg_envelope_level(&psg) == 1);

    /* Rewriting the shape restarts it, even when the value is unchanged. */
    psg_write_register(&psg, 13, 0x0c);
    assert(psg_envelope_level(&psg) == 0);
    assert(psg.envelope_counter == 0);

    /* Period zero is twice as fast as period one. */
    psg_write_register(&psg, 11, 0);
    psg_advance_ticks(&psg, 3);
    assert(psg_envelope_level(&psg) == 0);
    psg_advance_ticks(&psg, 1);
    assert(psg_envelope_level(&psg) == 1);
}

static void test_envelope_shapes(void) {
    Psg psg;

    psg_init(&psg, PSG_VARIANT_YM2149);
    psg_write_register(&psg, 11, 1);

    psg_write_register(&psg, 13, 0x09); /* descend, hold low */
    assert(psg_envelope_level(&psg) == 31);
    psg_advance_ticks(&psg, 32 * 8);
    assert(psg.envelope_holding);
    assert(psg_envelope_level(&psg) == 0);

    psg_write_register(&psg, 13, 0x0b); /* descend, hold high */
    psg_advance_ticks(&psg, 32 * 8);
    assert(psg.envelope_holding);
    assert(psg_envelope_level(&psg) == 31);

    psg_write_register(&psg, 13, 0x0a); /* repeating triangle */
    assert(psg_envelope_level(&psg) == 31);
    psg_advance_ticks(&psg, 32 * 8);
    assert(!psg.envelope_holding);
    assert(psg_envelope_level(&psg) == 0);
    psg_advance_ticks(&psg, 8);
    assert(psg_envelope_level(&psg) == 1);

    psg_write_register(&psg, 13, 0x0c); /* repeating rising saw */
    psg_advance_ticks(&psg, 32 * 8);
    assert(!psg.envelope_holding);
    assert(psg_envelope_level(&psg) == 0);
}

static void test_mixer_dac_and_volume(void) {
    Psg psg;
    s16 samples[2048];
    bool positive = false;
    bool negative = false;

    psg_init(&psg, PSG_VARIANT_YM2149);
    psg_set_volume(&psg, 100);
    psg_write_register(&psg, 0, 1);
    psg_write_register(&psg, 7, 0x3e);
    psg_write_register(&psg, 8, 0x0f);
    psg_render(&psg, samples, 2048, 8000, 1000);
    for (unsigned i = 0; i < 2048; ++i) {
        positive |= samples[i] > 0;
        negative |= samples[i] < 0;
    }
    assert(positive);
    assert(negative);

    /* Disabling both generators holds the mixer high, which is the
     * volume-register DAC path used by sample players. */
    psg_reset(&psg);
    psg_write_register(&psg, 7, 0x3f);
    memset(samples, 0, sizeof(samples));
    psg_render(&psg, samples, 8, 8000, 1000);
    psg_write_register(&psg, 8, 0x0f);
    memset(samples, 0, sizeof(samples));
    psg_render(&psg, samples, 8, 8000, 1000);
    positive = false;
    for (unsigned i = 0; i < 8; ++i)
        positive |= samples[i] != 0;
    assert(positive);

    psg_set_volume(&psg, 0);
    psg_reset(&psg);
    psg_write_register(&psg, 7, 0x3f);
    psg_write_register(&psg, 8, 0x0f);
    memset(samples, 0x7f, sizeof(samples));
    psg_render(&psg, samples, 128, 8000, 1000);
    for (unsigned i = 0; i < 128; ++i)
        assert(samples[i] == 0);
}

int main(void) {
    test_register_latch_and_variant_readback();
    test_tone_and_noise_periods();
    test_envelope_period_and_variants();
    test_envelope_shapes();
    test_mixer_dac_and_volume();
    return 0;
}
