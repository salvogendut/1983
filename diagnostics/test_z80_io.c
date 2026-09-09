#include "z80.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    u8 memory[0x10000];
    u16 port;
    u8 value;
    unsigned reads, writes, ticks;
    int elapsed, ticked, io_time;
} IoBus;

static u8 read_memory(void *context, u16 address) {
    return ((IoBus *)context)->memory[address];
}

static void write_memory(void *context, u16 address, u8 value) {
    ((IoBus *)context)->memory[address] = value;
}

static u8 read_io(void *context, u16 port) {
    IoBus *bus = context;
    ++bus->reads;
    bus->port = port;
    bus->io_time = bus->elapsed;
    return 0x38;
}

static void write_io(void *context, u16 port, u8 value) {
    IoBus *bus = context;
    ++bus->writes;
    bus->port = port;
    bus->value = value;
    bus->io_time = bus->elapsed;
}

static void tick(void *context, int cycles) {
    IoBus *bus = context;
    assert(cycles > 0);
    ++bus->ticks;
    bus->elapsed += cycles;
    bus->ticked += cycles;
}

static void test_immediate_io(bool timed, bool input, u8 accumulator, u8 port,
                              u8 flags) {
    IoBus state;
    Z80 cpu;
    memset(&state, 0, sizeof(state));
    Z80Bus bus = {
        .mem_read = read_memory, .mem_write = write_memory,
        .io_read = read_io, .io_write = write_io,
        .tick = timed ? tick : NULL,
        .ticked_in_step = timed ? &state.ticked : NULL,
        .ctx = &state,
    };
    z80_init(&cpu);
    cpu.a = accumulator;
    cpu.f = flags; /* Immediate IN and OUT must preserve every flag. */
    state.memory[0] = input ? 0xdb : 0xd3;
    state.memory[1] = port;
    state.ticked = timed ? 99 : 0; /* z80_step must reset the counter. */

    int total = z80_step(&cpu, &bus);
    assert(total == 11);
    assert(cpu.pc == 2);
    assert(state.port == (u16)((accumulator << 8) | port));
    assert(state.reads == (unsigned)input);
    assert(state.writes == (unsigned)!input);
    assert(cpu.a == (input ? 0x38 : accumulator));
    assert(input || state.value == accumulator);
    assert(cpu.f == flags);
    assert(state.ticks == (unsigned)timed);
    assert(state.io_time == (timed ? 8 : 0));
    assert(state.ticked == (timed ? 8 : 0));
    assert(state.ticked <= total);
    /* Like msx_run_frame, account only for the unticked remainder. */
    state.elapsed += total - state.ticked;
    assert(state.elapsed == 11);

    total = z80_step(&cpu, &bus); /* NOP, not another I/O operation. */
    assert(total == 4);
    assert(cpu.pc == 3);
    assert(state.ticked == 0);
    state.elapsed += total;
    assert(state.elapsed == 15);
    assert(state.ticks == (unsigned)timed);
}

int main(void) {
    const u8 values[] = {0x00, 0x5a, 0xff};
    const u8 ports[] = {0x00, 0x98, 0x99, 0xff};
    for (unsigned timed = 0; timed < 2; ++timed)
        for (unsigned input = 0; input < 2; ++input)
            for (unsigned a = 0; a < sizeof(values); ++a)
                for (unsigned p = 0; p < sizeof(ports); ++p)
                    for (unsigned flags = 0; flags < 2; ++flags)
                        test_immediate_io(timed, input, values[a], ports[p],
                                          flags ? 0xff : 0x00);
    puts("immediate Z80 I/O: 96 phase/port/value/flags/null-hook cases passed");
    return 0;
}
