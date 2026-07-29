#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define CASSETTE_SAMPLE_RATE 14976u

typedef struct {
    s8 *samples;
    size_t sample_count;
    size_t position;
    u64 cycle_fraction;
    u64 last_cycle;
    bool mounted;
    bool motor;
    bool output;
} Cassette;

void cassette_init(Cassette *cassette);
void cassette_destroy(Cassette *cassette);
void cassette_reset(Cassette *cassette, u64 current_cycle);

int cassette_mount(Cassette *cassette, const u8 *data, size_t size,
                   u64 current_cycle);
int cassette_mount_file(Cassette *cassette, const char *path,
                        u64 current_cycle);
void cassette_eject(Cassette *cassette, u64 current_cycle);
void cassette_rewind(Cassette *cassette, u64 current_cycle);

void cassette_set_motor(Cassette *cassette, bool motor,
                        u64 current_cycle);
void cassette_set_output(Cassette *cassette, bool output,
                         u64 current_cycle);
bool cassette_input(Cassette *cassette, u64 current_cycle);

bool cassette_is_mounted(const Cassette *cassette);
bool cassette_is_motor_on(const Cassette *cassette);
bool cassette_is_rolling(Cassette *cassette, u64 current_cycle);
bool cassette_at_end(Cassette *cassette, u64 current_cycle);
u64 cassette_position_ms(Cassette *cassette, u64 current_cycle);
u64 cassette_duration_ms(const Cassette *cassette);
