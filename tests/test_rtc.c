#include "rtc.h"

#include <assert.h>
#include <stdbool.h>

#define TEST_CPU_HZ 3579545u

static void select_register(MsxRtc *rtc, unsigned reg) {
    assert(reg < 16);
    rtc_select(rtc, (u8)reg);
}

static void write_register(MsxRtc *rtc, unsigned reg, u8 value) {
    select_register(rtc, reg);
    rtc_write_data(rtc, value);
}

static u8 read_register(MsxRtc *rtc, unsigned reg) {
    select_register(rtc, reg);
    return rtc_read_data(rtc);
}

static void select_block(MsxRtc *rtc, u8 block, bool timer_enabled) {
    write_register(rtc, 13,
                   (u8)(block | (timer_enabled ? 0x08 : 0x00)));
}

static void test_control_registers_and_banked_ram(void) {
    MsxRtc rtc;

    rtc_init(&rtc);
    assert(read_register(&rtc, 13) == 0x08);
    assert(read_register(&rtc, 14) == 0x0f);
    assert(read_register(&rtc, 15) == 0x0f);

    select_block(&rtc, 2, false);
    write_register(&rtc, 4, 0xbe);
    assert(read_register(&rtc, 4) == 0x0e);
    select_block(&rtc, 3, false);
    assert(read_register(&rtc, 4) == 0);
    write_register(&rtc, 4, 7);
    select_block(&rtc, 2, false);
    assert(read_register(&rtc, 4) == 0x0e);

    /* Time/alarm registers apply the RP-5C01 per-register masks. */
    select_block(&rtc, 0, false);
    write_register(&rtc, 1, 0x0f);
    assert(read_register(&rtc, 1) == 0x07);
    select_block(&rtc, 1, false);
    write_register(&rtc, 0, 0x0f);
    assert(read_register(&rtc, 0) == 0);

    for (unsigned reg = 2; reg <= 8; ++reg)
        write_register(&rtc, reg, (u8)reg);
    write_register(&rtc, 15, 1);
    for (unsigned reg = 2; reg <= 8; ++reg)
        assert(read_register(&rtc, reg) == 0);

    /* Reset restores control state but preserves battery-backed registers. */
    select_block(&rtc, 2, false);
    write_register(&rtc, 4, 9);
    rtc_reset(&rtc);
    assert(rtc.latch == 0);
    assert(read_register(&rtc, 13) == 0x08);
    select_block(&rtc, 2, false);
    assert(read_register(&rtc, 4) == 9);
}

static void set_end_of_1983(MsxRtc *rtc) {
    select_block(rtc, 1, false);
    write_register(rtc, 10, 1); /* 24-hour mode */
    write_register(rtc, 11, 3); /* 1983 in the four-year cycle */

    select_block(rtc, 0, false);
    write_register(rtc, 0, 9);
    write_register(rtc, 1, 5);
    write_register(rtc, 2, 9);
    write_register(rtc, 3, 5);
    write_register(rtc, 4, 3);
    write_register(rtc, 5, 2);
    write_register(rtc, 6, 6);
    write_register(rtc, 7, 1);
    write_register(rtc, 8, 3);
    write_register(rtc, 9, 2);
    write_register(rtc, 10, 1);
    write_register(rtc, 11, 3);
    write_register(rtc, 12, 8);
}

static void test_emulated_time_and_fraction_reset(void) {
    MsxRtc rtc;

    rtc_init(&rtc);
    set_end_of_1983(&rtc);
    select_block(&rtc, 0, true);
    rtc_advance(&rtc, TEST_CPU_HZ - 1, TEST_CPU_HZ);
    assert(read_register(&rtc, 0) == 9);
    rtc_advance(&rtc, 1, TEST_CPU_HZ);

    assert(read_register(&rtc, 0) == 0);
    assert(read_register(&rtc, 1) == 0);
    assert(read_register(&rtc, 2) == 0);
    assert(read_register(&rtc, 3) == 0);
    assert(read_register(&rtc, 4) == 0);
    assert(read_register(&rtc, 5) == 0);
    assert(read_register(&rtc, 6) == 0);
    assert(read_register(&rtc, 7) == 1);
    assert(read_register(&rtc, 8) == 0);
    assert(read_register(&rtc, 9) == 1);
    assert(read_register(&rtc, 10) == 0);
    assert(read_register(&rtc, 11) == 4);
    assert(read_register(&rtc, 12) == 8);
    select_block(&rtc, 1, false);
    assert(read_register(&rtc, 11) == 0);

    /* A stopped clock discards elapsed CPU time. */
    select_block(&rtc, 0, false);
    rtc_advance(&rtc, TEST_CPU_HZ * 2, TEST_CPU_HZ);
    assert(read_register(&rtc, 0) == 0);

    /* RESET bit 1 clears the sub-second divider. */
    select_block(&rtc, 0, true);
    rtc_advance(&rtc, TEST_CPU_HZ / 2, TEST_CPU_HZ);
    write_register(&rtc, 15, 2);
    rtc_advance(&rtc, TEST_CPU_HZ / 2, TEST_CPU_HZ);
    assert(read_register(&rtc, 0) == 0);
    rtc_advance(&rtc, (TEST_CPU_HZ + 1) / 2, TEST_CPU_HZ);
    assert(read_register(&rtc, 0) == 1);
}

int main(void) {
    test_control_registers_and_banked_ram();
    test_emulated_time_and_fraction_reset();
    return 0;
}
