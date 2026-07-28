#pragma once

#include "types.h"

#define MSX_RTC_BLOCK_COUNT 4
#define MSX_RTC_BLOCK_SIZE 13

typedef struct {
    u8 registers[MSX_RTC_BLOCK_COUNT][MSX_RTC_BLOCK_SIZE];
    u8 latch;
    u8 mode;
    u8 test;
    u8 reset;
    u64 cycle_accumulator;
} MsxRtc;

void rtc_init(MsxRtc *rtc);
void rtc_reset(MsxRtc *rtc);
void rtc_advance(MsxRtc *rtc, unsigned cycles, unsigned cpu_hz);

void rtc_select(MsxRtc *rtc, u8 value);
u8   rtc_read_data(const MsxRtc *rtc);
void rtc_write_data(MsxRtc *rtc, u8 value);
