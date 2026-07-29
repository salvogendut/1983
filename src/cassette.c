#include "cassette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASSETTE_CPU_HZ 3579545u
#define CASSETTE_MAX_FILE_SIZE (8u * 1024u * 1024u)
#define CASSETTE_MAX_SAMPLES (128u * 1024u * 1024u)

#define CASSETTE_SHORT_SILENCE CASSETTE_SAMPLE_RATE
#define CASSETTE_LONG_SILENCE (CASSETTE_SAMPLE_RATE * 2u)
#define CASSETTE_LONG_HEADER_BITS 8000u
#define CASSETTE_SHORT_HEADER_BITS 2000u

static const u8 cas_header[8] = {
    0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74
};

typedef struct {
    s8 *samples;
    size_t count;
    size_t capacity;
    size_t data_bytes;
    bool failed;
} CassetteBuilder;

typedef enum {
    CAS_FILE_UNKNOWN = 0,
    CAS_FILE_ASCII,
    CAS_FILE_BINARY,
    CAS_FILE_BASIC
} CasFileType;

static bool bytes_equal(const u8 *data, const u8 *pattern,
                        size_t size) {
    return memcmp(data, pattern, size) == 0;
}

static bool is_header_at(const u8 *data, size_t size, size_t position) {
    return position <= size &&
           size - position >= sizeof(cas_header) &&
           bytes_equal(data + position, cas_header,
                       sizeof(cas_header));
}

static bool builder_reserve(CassetteBuilder *builder, size_t additional) {
    size_t required;
    size_t capacity;
    s8 *samples;

    if (builder->failed)
        return false;
    if (additional > CASSETTE_MAX_SAMPLES - builder->count) {
        builder->failed = true;
        return false;
    }
    required = builder->count + additional;
    if (required <= builder->capacity)
        return true;
    capacity = builder->capacity ? builder->capacity : 65536u;
    while (capacity < required) {
        if (capacity > CASSETTE_MAX_SAMPLES / 2u) {
            capacity = CASSETTE_MAX_SAMPLES;
            break;
        }
        capacity *= 2u;
    }
    samples = realloc(builder->samples, capacity);
    if (!samples) {
        builder->failed = true;
        return false;
    }
    builder->samples = samples;
    builder->capacity = capacity;
    return true;
}

static void builder_repeat(CassetteBuilder *builder, size_t count,
                           s8 value) {
    if (!builder_reserve(builder, count))
        return;
    for (size_t i = 0; i < count; ++i)
        builder->samples[builder->count + i] = value;
    builder->count += count;
}

static void builder_bit(CassetteBuilder *builder, bool bit) {
    static const s8 zero[4] = { 127, 127, -127, -127 };
    static const s8 one[4] = { 127, -127, 127, -127 };
    const s8 *samples = bit ? one : zero;

    if (!builder_reserve(builder, 4))
        return;
    memcpy(builder->samples + builder->count, samples, 4);
    builder->count += 4;
}

static void builder_header(CassetteBuilder *builder, unsigned bits) {
    for (unsigned i = 0; i < bits && !builder->failed; ++i)
        builder_bit(builder, true);
}

static void builder_byte(CassetteBuilder *builder, u8 value) {
    builder_bit(builder, false);
    for (unsigned bit = 0; bit < 8; ++bit)
        builder_bit(builder, (value & (1u << bit)) != 0);
    builder_bit(builder, true);
    builder_bit(builder, true);
    ++builder->data_bytes;
}

static CasFileType cas_file_type(const u8 *data, size_t size,
                                 size_t position) {
    u8 value;

    if (position > size || size - position < 10)
        return CAS_FILE_UNKNOWN;
    value = data[position];
    for (unsigned i = 1; i < 10; ++i)
        if (data[position + i] != value)
            return CAS_FILE_UNKNOWN;
    switch (value) {
        case 0xea: return CAS_FILE_ASCII;
        case 0xd0: return CAS_FILE_BINARY;
        case 0xd3: return CAS_FILE_BASIC;
        default:   return CAS_FILE_UNKNOWN;
    }
}

static bool builder_data(CassetteBuilder *builder, const u8 *data,
                         size_t size, size_t *position) {
    bool eof = false;

    while (*position < size) {
        if (is_header_at(data, size, *position))
            break;
        builder_byte(builder, data[(*position)++]);
        if (data[*position - 1] == 0x1a)
            eof = true;
        if (builder->failed)
            break;
    }
    return eof;
}

static int build_msx_cas(CassetteBuilder *builder, const u8 *data,
                         size_t size) {
    size_t position = 0;
    bool found_header = false;

    while (position < size) {
        CasFileType type;

        if (!is_header_at(data, size, position)) {
            ++position;
            continue;
        }
        found_header = true;
        position += sizeof(cas_header);
        builder_repeat(builder, CASSETTE_LONG_SILENCE, 0);
        builder_header(builder, CASSETTE_LONG_HEADER_BITS);
        type = cas_file_type(data, size, position);

        if (type == CAS_FILE_ASCII) {
            (void)builder_data(builder, data, size, &position);
            while (is_header_at(data, size, position)) {
                bool eof;

                position += sizeof(cas_header);
                builder_repeat(builder, CASSETTE_SHORT_SILENCE, 0);
                builder_header(builder, CASSETTE_SHORT_HEADER_BITS);
                eof = builder_data(
                    builder, data, size, &position);
                if (eof)
                    break;
            }
        } else if (type == CAS_FILE_BINARY ||
                   type == CAS_FILE_BASIC) {
            (void)builder_data(builder, data, size, &position);
            if (is_header_at(data, size, position)) {
                builder_repeat(builder, CASSETTE_SHORT_SILENCE, 0);
                builder_header(builder, CASSETTE_SHORT_HEADER_BITS);
                position += sizeof(cas_header);
                (void)builder_data(
                    builder, data, size, &position);
            }
        } else {
            (void)builder_data(builder, data, size, &position);
        }
        if (builder->failed)
            return -1;
    }
    return found_header && builder->data_bytes ? 0 : -1;
}

static void cassette_sync(Cassette *cassette, u64 current_cycle) {
    u64 delta;
    u64 whole_seconds;
    u64 remaining;
    u64 advance;
    u64 scaled_remainder;

    if (!cassette)
        return;
    if (current_cycle < cassette->last_cycle) {
        cassette->last_cycle = current_cycle;
        return;
    }
    delta = current_cycle - cassette->last_cycle;
    cassette->last_cycle = current_cycle;
    if (!delta || !cassette->mounted || !cassette->motor ||
        cassette->position >= cassette->sample_count)
        return;

    remaining = cassette->sample_count - cassette->position;
    whole_seconds = delta / CASSETTE_CPU_HZ;
    if (whole_seconds >=
        (remaining + CASSETTE_SAMPLE_RATE - 1u) /
        CASSETTE_SAMPLE_RATE) {
        cassette->position = cassette->sample_count;
        cassette->cycle_fraction = 0;
        return;
    }
    advance = whole_seconds * CASSETTE_SAMPLE_RATE;
    scaled_remainder =
        (delta % CASSETTE_CPU_HZ) * CASSETTE_SAMPLE_RATE +
        cassette->cycle_fraction;
    advance += scaled_remainder / CASSETTE_CPU_HZ;
    cassette->cycle_fraction =
        scaled_remainder % CASSETTE_CPU_HZ;
    if (advance >= remaining) {
        cassette->position = cassette->sample_count;
        cassette->cycle_fraction = 0;
    } else {
        cassette->position += (size_t)advance;
    }
}

void cassette_init(Cassette *cassette) {
    if (!cassette)
        return;
    memset(cassette, 0, sizeof(*cassette));
    cassette->output = true;
}

void cassette_destroy(Cassette *cassette) {
    if (!cassette)
        return;
    free(cassette->samples);
    cassette_init(cassette);
}

void cassette_reset(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return;
    cassette_sync(cassette, current_cycle);
    cassette->motor = false;
    cassette->output = true;
    cassette->last_cycle = 0;
}

int cassette_mount(Cassette *cassette, const u8 *data, size_t size,
                   u64 current_cycle) {
    CassetteBuilder builder = { 0 };

    if (!cassette || !data || !size ||
        size > CASSETTE_MAX_FILE_SIZE ||
        build_msx_cas(&builder, data, size) != 0) {
        free(builder.samples);
        return -1;
    }
    cassette_sync(cassette, current_cycle);
    free(cassette->samples);
    cassette->samples = builder.samples;
    cassette->sample_count = builder.count;
    cassette->position = 0;
    cassette->cycle_fraction = 0;
    cassette->last_cycle = current_cycle;
    cassette->mounted = true;
    return 0;
}

int cassette_mount_file(Cassette *cassette, const char *path,
                        u64 current_cycle) {
    FILE *file;
    u8 *data;
    long length;
    size_t got;
    int result;

    if (!cassette || !path || !path[0])
        return -1;
    file = fopen(path, "rb");
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 ||
        (unsigned long)length > CASSETTE_MAX_FILE_SIZE ||
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
    result = cassette_mount(cassette, data, got, current_cycle);
    free(data);
    return result;
}

void cassette_eject(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return;
    cassette_sync(cassette, current_cycle);
    free(cassette->samples);
    cassette->samples = NULL;
    cassette->sample_count = 0;
    cassette->position = 0;
    cassette->cycle_fraction = 0;
    cassette->last_cycle = current_cycle;
    cassette->mounted = false;
}

void cassette_rewind(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return;
    cassette_sync(cassette, current_cycle);
    cassette->position = 0;
    cassette->cycle_fraction = 0;
    cassette->last_cycle = current_cycle;
}

void cassette_set_motor(Cassette *cassette, bool motor,
                        u64 current_cycle) {
    if (!cassette)
        return;
    cassette_sync(cassette, current_cycle);
    cassette->motor = motor;
}

void cassette_set_output(Cassette *cassette, bool output,
                         u64 current_cycle) {
    if (!cassette)
        return;
    cassette_sync(cassette, current_cycle);
    cassette->output = output;
}

bool cassette_input(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return true;
    cassette_sync(cassette, current_cycle);
    if (!cassette->mounted ||
        cassette->position >= cassette->sample_count)
        return true;
    return cassette->samples[cassette->position] >= 0;
}

bool cassette_is_mounted(const Cassette *cassette) {
    return cassette && cassette->mounted;
}

bool cassette_is_motor_on(const Cassette *cassette) {
    return cassette && cassette->motor;
}

bool cassette_is_rolling(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return false;
    cassette_sync(cassette, current_cycle);
    return cassette->mounted && cassette->motor &&
           cassette->position < cassette->sample_count;
}

bool cassette_at_end(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return false;
    cassette_sync(cassette, current_cycle);
    return cassette->mounted &&
           cassette->position >= cassette->sample_count;
}

u64 cassette_position_ms(Cassette *cassette, u64 current_cycle) {
    if (!cassette)
        return 0;
    cassette_sync(cassette, current_cycle);
    return (u64)cassette->position * 1000u /
           CASSETTE_SAMPLE_RATE;
}

u64 cassette_duration_ms(const Cassette *cassette) {
    if (!cassette)
        return 0;
    return (u64)cassette->sample_count * 1000u /
           CASSETTE_SAMPLE_RATE;
}
