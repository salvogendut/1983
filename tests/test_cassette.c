#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cassette.h"

#define TEST_CPU_HZ 3579545u

static const u8 header[8] = {
    0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74
};

static size_t make_basic_cas(u8 *data, size_t capacity) {
    size_t position = 0;

    assert(capacity >= 31);
    memcpy(data + position, header, sizeof(header));
    position += sizeof(header);
    memset(data + position, 0xd3, 10);
    position += 10;
    data[position++] = 0x42;
    memcpy(data + position, header, sizeof(header));
    position += sizeof(header);
    data[position++] = 0x10;
    data[position++] = 0x1a;
    return position;
}

static void test_parser_and_waveform(void) {
    u8 data[64];
    size_t size = make_basic_cas(data, sizeof(data));
    Cassette cassette;
    const size_t long_silence = CASSETTE_SAMPLE_RATE * 2u;
    const size_t long_header = 8000u * 4u;
    const size_t first_data = 11u * 11u * 4u;
    const size_t short_silence = CASSETTE_SAMPLE_RATE;
    const size_t short_header = 2000u * 4u;
    const size_t second_data = 2u * 11u * 4u;
    const size_t expected =
        long_silence + long_header + first_data +
        short_silence + short_header + second_data;

    cassette_init(&cassette);
    assert(cassette_mount(&cassette, data, size, 0) == 0);
    assert(cassette_is_mounted(&cassette));
    assert(cassette.sample_count == expected);
    assert(cassette_duration_ms(&cassette) ==
           (u64)expected * 1000u / CASSETTE_SAMPLE_RATE);

    assert(cassette.samples[0] == 0);
    assert(cassette.samples[long_silence + 0] == 127);
    assert(cassette.samples[long_silence + 1] == -127);
    assert(cassette.samples[long_silence + 2] == 127);
    assert(cassette.samples[long_silence + 3] == -127);

    /* The first byte starts with a zero start bit. */
    assert(cassette.samples[long_silence + long_header + 0] == 127);
    assert(cassette.samples[long_silence + long_header + 1] == 127);
    assert(cassette.samples[long_silence + long_header + 2] == -127);
    assert(cassette.samples[long_silence + long_header + 3] == -127);

    cassette_destroy(&cassette);
}

static void test_timing_controls_and_atomic_mount(void) {
    static const u8 invalid[] = { 0x00, 0x01, 0x02 };
    u8 data[64];
    size_t size = make_basic_cas(data, sizeof(data));
    Cassette cassette;
    s8 *mounted_samples;
    size_t mounted_count;

    cassette_init(&cassette);
    assert(cassette_input(&cassette, 0));
    assert(cassette_mount(&cassette, data, size, 100) == 0);
    mounted_samples = cassette.samples;
    mounted_count = cassette.sample_count;

    cassette_set_motor(&cassette, true, 100);
    assert(cassette_is_motor_on(&cassette));
    assert(cassette_is_rolling(&cassette, 100));
    assert(cassette_position_ms(
               &cassette, 100 + TEST_CPU_HZ) == 1000);
    assert(cassette.position == CASSETTE_SAMPLE_RATE);

    cassette_set_motor(
        &cassette, false, 100 + TEST_CPU_HZ);
    assert(!cassette_is_rolling(
        &cassette, 100 + 2u * TEST_CPU_HZ));
    assert(cassette.position == CASSETTE_SAMPLE_RATE);

    /* A rejected replacement leaves the inserted tape untouched. */
    assert(cassette_mount(
               &cassette, invalid, sizeof(invalid),
               100 + 2u * TEST_CPU_HZ) != 0);
    assert(cassette.samples == mounted_samples);
    assert(cassette.sample_count == mounted_count);
    assert(cassette.position == CASSETTE_SAMPLE_RATE);

    cassette_rewind(&cassette, 100 + 2u * TEST_CPU_HZ);
    assert(cassette.position == 0);
    cassette_set_motor(
        &cassette, true, 100 + 2u * TEST_CPU_HZ);
    assert(cassette_input(&cassette, 100 + 2u * TEST_CPU_HZ));

    cassette_set_output(
        &cassette, false, 100 + 2u * TEST_CPU_HZ);
    assert(!cassette.output);
    cassette_reset(&cassette, 100 + 2u * TEST_CPU_HZ);
    assert(cassette.position == 0);
    assert(!cassette.motor);
    assert(cassette.output);

    cassette_set_motor(&cassette, true, 0);
    assert(cassette_at_end(
        &cassette, (u64)TEST_CPU_HZ * 1000u));
    assert(!cassette_is_rolling(
        &cassette, (u64)TEST_CPU_HZ * 1000u));

    cassette_eject(
        &cassette, (u64)TEST_CPU_HZ * 1000u);
    assert(!cassette_is_mounted(&cassette));
    assert(cassette_input(
        &cassette, (u64)TEST_CPU_HZ * 1000u));
    cassette_destroy(&cassette);
}

static void test_file_mount(void) {
    const char *path = "test-cassette-image.tmp";
    u8 data[64];
    size_t size = make_basic_cas(data, sizeof(data));
    Cassette cassette;
    FILE *file;

    file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    assert(fclose(file) == 0);

    cassette_init(&cassette);
    assert(cassette_mount_file(&cassette, path, 0) == 0);
    assert(cassette_is_mounted(&cassette));
    assert(cassette_mount_file(
               &cassette, "missing-cassette.cas", 0) != 0);
    assert(cassette_is_mounted(&cassette));
    cassette_destroy(&cassette);
    assert(remove(path) == 0);
}

int main(void) {
    test_parser_and_waveform();
    test_timing_controls_and_atomic_mount();
    test_file_mount();
    puts("cassette parser, waveform, and transport tests passed");
    return 0;
}
