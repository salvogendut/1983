#include "rtc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

static unsigned read_pair(MsxRtc *rtc, unsigned low) {
    return read_register(rtc, low) +
           10u * read_register(rtc, low + 1);
}

static void set_date_time(MsxRtc *rtc, bool twenty_four_hour,
                          unsigned hours, unsigned minutes,
                          unsigned seconds, unsigned weekday,
                          unsigned day, unsigned month,
                          unsigned year, unsigned leap_year) {
    unsigned encoded_hour = hours;

    select_block(rtc, 1, false);
    write_register(rtc, 10, twenty_four_hour ? 1 : 0);
    write_register(rtc, 11, (u8)leap_year);
    if (!twenty_four_hour && hours >= 12)
        encoded_hour = hours - 12 + 20;

    select_block(rtc, 0, false);
    write_register(rtc, 0, (u8)(seconds % 10));
    write_register(rtc, 1, (u8)(seconds / 10));
    write_register(rtc, 2, (u8)(minutes % 10));
    write_register(rtc, 3, (u8)(minutes / 10));
    write_register(rtc, 4, (u8)(encoded_hour % 10));
    write_register(rtc, 5, (u8)(encoded_hour / 10));
    write_register(rtc, 6, (u8)weekday);
    write_register(rtc, 7, (u8)(day % 10));
    write_register(rtc, 8, (u8)(day / 10));
    write_register(rtc, 9, (u8)(month % 10));
    write_register(rtc, 10, (u8)(month / 10));
    write_register(rtc, 11, (u8)(year % 10));
    write_register(rtc, 12, (u8)(year / 10));
}

static void test_control_registers_and_banked_ram(void) {
    MsxRtc rtc;

    rtc_init_at(&rtc, 0);
    assert(!rtc_dirty(&rtc));
    assert(read_register(&rtc, 13) == 0x08);
    assert(read_register(&rtc, 14) == 0x0f);
    assert(read_register(&rtc, 15) == 0x0f);

    select_block(&rtc, 2, false);
    write_register(&rtc, 4, 0xbe);
    assert(read_register(&rtc, 4) == 0x0e);
    assert(rtc_dirty(&rtc));
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
    set_date_time(rtc, true, 23, 59, 59, 6, 31, 12, 83, 3);
}

static void test_emulated_time_and_fraction_reset(void) {
    MsxRtc rtc;

    rtc_init_at(&rtc, 0);
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

static void test_hour_modes_leap_years_and_test_register(void) {
    MsxRtc rtc;

    rtc_init_at(&rtc, 0);

    /* 11:59:59 AM becomes the RP-5C01's encoded 12:00 PM. */
    set_date_time(&rtc, false, 11, 59, 59, 1, 1, 1, 84, 0);
    select_block(&rtc, 0, true);
    rtc_advance_seconds(&rtc, 1);
    select_block(&rtc, 0, false);
    assert(read_pair(&rtc, 0) == 0);
    assert(read_pair(&rtc, 2) == 0);
    assert(read_pair(&rtc, 4) == 20);
    assert(read_pair(&rtc, 7) == 1);

    /* 11:59:59 PM wraps to 00:00:00 in the same 12-hour encoding. */
    set_date_time(&rtc, false, 23, 59, 59, 1, 1, 1, 84, 0);
    rtc_advance_seconds(&rtc, 1);
    select_block(&rtc, 0, false);
    assert(read_pair(&rtc, 4) == 0);
    assert(read_pair(&rtc, 7) == 2);

    set_date_time(&rtc, true, 23, 59, 59, 3, 28, 2, 84, 0);
    rtc_advance_seconds(&rtc, 1);
    select_block(&rtc, 0, false);
    assert(read_pair(&rtc, 7) == 29);
    assert(read_pair(&rtc, 9) == 2);
    rtc_advance_seconds(&rtc, 24u * 60u * 60u);
    select_block(&rtc, 0, false);
    assert(read_pair(&rtc, 7) == 1);
    assert(read_pair(&rtc, 9) == 3);

    set_date_time(&rtc, true, 23, 59, 59, 5, 28, 2, 85, 1);
    rtc_advance_seconds(&rtc, 1);
    select_block(&rtc, 0, false);
    assert(read_pair(&rtc, 7) == 1);
    assert(read_pair(&rtc, 9) == 3);

    /*
     * TEST bit 0 accelerates seconds at 16,384 Hz even when the normal
     * timer is stopped.
     */
    set_date_time(&rtc, true, 0, 0, 0, 0, 1, 1, 83, 3);
    select_block(&rtc, 0, false);
    write_register(&rtc, 14, 1);
    rtc_advance(&rtc, (TEST_CPU_HZ + 16383u) / 16384u,
                TEST_CPU_HZ);
    select_block(&rtc, 0, false);
    assert(read_pair(&rtc, 0) == 1);
}

static void assert_start_of_1984(MsxRtc *rtc, unsigned seconds) {
    select_block(rtc, 0, false);
    assert(read_pair(rtc, 0) == seconds);
    assert(read_pair(rtc, 2) == 0);
    assert(read_pair(rtc, 4) == 0);
    assert(read_pair(rtc, 7) == 1);
    assert(read_pair(rtc, 9) == 1);
    assert(read_pair(rtc, 11) == 84);
    select_block(rtc, 1, false);
    assert(read_register(rtc, 11) == 0);
}

static void test_persistent_cmos_and_offline_continuity(void) {
    const char *path = "test-rtc-cmos.tmp";
    const char *blocked_parent = "test-rtc-parent.tmp";
    const char *blocked_path = "test-rtc-parent.tmp/clock.cmos";
    char error[160];
    MsxRtc saved;
    MsxRtc loaded;
    MsxRtc unchanged;
    FILE *file;
    long size;

    (void)remove(path);
    (void)remove(blocked_parent);

    rtc_init_at(&saved, 0);
    set_end_of_1983(&saved);
    select_block(&saved, 2, false);
    write_register(&saved, 4, 0x0e);
    write_register(&saved, 12, 0x0a);
    select_block(&saved, 3, false);
    write_register(&saved, 7, 0x0b);
    select_block(&saved, 0, true);
    assert(rtc_dirty(&saved));
    assert(rtc_save_persistence(
               &saved, path, 1000, error, sizeof(error)) == 0);
    assert(!error[0]);
    assert(!rtc_dirty(&saved));
    file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size == MSX_RTC_PERSISTENCE_SIZE);
    assert(fclose(file) == 0);

    rtc_init_at(&loaded, 0);
    assert(rtc_load_persistence(
               &loaded, path, 1000, error, sizeof(error)) == 0);
    assert(!rtc_dirty(&loaded));
    select_block(&loaded, 2, false);
    assert(read_register(&loaded, 4) == 0x0e);
    assert(read_register(&loaded, 12) == 0x0a);
    select_block(&loaded, 3, false);
    assert(read_register(&loaded, 7) == 0x0b);

    /* A running battery clock catches up while the emulator is closed. */
    rtc_init_at(&loaded, 0);
    assert(rtc_load_persistence(
               &loaded, path, 1002, error, sizeof(error)) == 0);
    assert(rtc_dirty(&loaded));
    assert_start_of_1984(&loaded, 1);

    /* A guest-stopped timer must not gain offline time. */
    rtc_init_at(&saved, 0);
    set_end_of_1983(&saved);
    select_block(&saved, 0, false);
    assert(rtc_save_persistence(
               &saved, path, 2000, error, sizeof(error)) == 0);
    rtc_init_at(&loaded, 0);
    assert(rtc_load_persistence(
               &loaded, path, 5000, error, sizeof(error)) == 0);
    assert(!rtc_dirty(&loaded));
    select_block(&loaded, 0, false);
    assert(read_pair(&loaded, 0) == 59);
    assert(read_pair(&loaded, 7) == 31);
    assert(read_pair(&loaded, 9) == 12);

    /* Missing state is a clean first-run condition. */
    assert(remove(path) == 0);
    rtc_init_at(&loaded, 0);
    unchanged = loaded;
    assert(rtc_load_persistence(
               &loaded, path, 5000, error, sizeof(error)) == 1);
    assert(!error[0]);
    assert(memcmp(&loaded, &unchanged, sizeof(loaded)) == 0);

    /* Checksum corruption is rejected without changing the live clock. */
    rtc_init_at(&saved, 0);
    set_end_of_1983(&saved);
    select_block(&saved, 0, true);
    assert(rtc_save_persistence(
               &saved, path, 6000, error, sizeof(error)) == 0);
    file = fopen(path, "r+b");
    assert(file);
    assert(fseek(file, 40, SEEK_SET) == 0);
    {
        int value = fgetc(file);

        assert(value != EOF);
        assert(fseek(file, 40, SEEK_SET) == 0);
        assert(fputc(value ^ 0x01, file) != EOF);
    }
    assert(fclose(file) == 0);
    rtc_init_at(&loaded, 0);
    unchanged = loaded;
    assert(rtc_load_persistence(
               &loaded, path, 6000, error, sizeof(error)) == -1);
    assert(strstr(error, "corrupted"));
    assert(memcmp(&loaded, &unchanged, sizeof(loaded)) == 0);

    /*
     * A host-path failure leaves dirty state intact and cannot damage
     * an existing target because replacement is same-directory atomic.
     */
    file = fopen(blocked_parent, "wb");
    assert(file);
    assert(fputc(0x83, file) != EOF);
    assert(fclose(file) == 0);
    rtc_init_at(&saved, 0);
    select_block(&saved, 2, false);
    write_register(&saved, 0, 1);
    assert(rtc_dirty(&saved));
    assert(rtc_save_persistence(
               &saved, blocked_path, 7000,
               error, sizeof(error)) == -1);
    assert(error[0]);
    assert(rtc_dirty(&saved));

    assert(remove(path) == 0);
    assert(remove(blocked_parent) == 0);
}

int main(void) {
    test_control_registers_and_banked_ram();
    test_emulated_time_and_fraction_reset();
    test_hour_modes_leap_years_and_test_register();
    test_persistent_cmos_and_offline_continuity();
    return 0;
}
