#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define MSX_RTC_BLOCK_COUNT 4
#define MSX_RTC_BLOCK_SIZE 13
#define MSX_RTC_PERSISTENCE_SIZE 84

typedef struct {
    u8 registers[MSX_RTC_BLOCK_COUNT][MSX_RTC_BLOCK_SIZE];
    u8 latch;
    u8 mode;
    u8 test;
    u8 reset;
    u64 cycle_accumulator;
    u64 test_cycle_accumulator;
    bool dirty;
} MsxRtc;

void rtc_init(MsxRtc *rtc);
void rtc_init_at(MsxRtc *rtc, u64 host_seconds);
u64  rtc_host_seconds(void);
void rtc_reset(MsxRtc *rtc);
void rtc_advance(MsxRtc *rtc, unsigned cycles, unsigned cpu_hz);
void rtc_advance_seconds(MsxRtc *rtc, u64 seconds);

void rtc_select(MsxRtc *rtc, u8 value);
u8   rtc_read_data(const MsxRtc *rtc);
void rtc_write_data(MsxRtc *rtc, u8 value);

bool rtc_dirty(const MsxRtc *rtc);
int rtc_load_persistence(MsxRtc *rtc, const char *path,
                         u64 host_seconds,
                         char *error, size_t error_size);
int rtc_save_persistence(MsxRtc *rtc, const char *path,
                         u64 host_seconds,
                         char *error, size_t error_size);
