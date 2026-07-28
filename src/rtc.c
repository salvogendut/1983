#include "rtc.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

enum {
    RTC_MODE_REGISTER = 13,
    RTC_TEST_REGISTER = 14,
    RTC_RESET_REGISTER = 15,
    RTC_MODE_BLOCK_MASK = 0x03,
    RTC_MODE_TIMER_ENABLE = 0x08,
    RTC_RESET_ALARM = 0x01,
    RTC_RESET_FRACTION = 0x02,
};

static const u8 register_masks[MSX_RTC_BLOCK_COUNT]
                              [MSX_RTC_BLOCK_SIZE] = {
    {
        0x0f, 0x07, 0x0f, 0x07, 0x0f, 0x03, 0x07,
        0x0f, 0x03, 0x0f, 0x01, 0x0f, 0x0f,
    },
    {
        0x00, 0x00, 0x0f, 0x07, 0x0f, 0x03, 0x07,
        0x0f, 0x03, 0x00, 0x01, 0x03, 0x00,
    },
    {
        0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
        0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    },
    {
        0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
        0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    },
};

static unsigned decode_pair(const u8 *registers, unsigned low) {
    return registers[low] + 10u * registers[low + 1];
}

static void encode_pair(u8 *registers, unsigned low, unsigned value) {
    registers[low] = (u8)(value % 10);
    registers[low + 1] = (u8)(value / 10);
}

static bool twenty_four_hour_mode(const MsxRtc *rtc) {
    return (rtc->registers[1][10] & 1) != 0;
}

static unsigned decode_hour(const MsxRtc *rtc) {
    unsigned hour = decode_pair(rtc->registers[0], 4);

    if (!twenty_four_hour_mode(rtc) && hour >= 20)
        hour = hour - 20 + 12;
    return hour % 24;
}

static void encode_hour(MsxRtc *rtc, unsigned hour) {
    if (!twenty_four_hour_mode(rtc) && hour >= 12)
        hour = hour - 12 + 20;
    encode_pair(rtc->registers[0], 4, hour);
}

static unsigned days_in_month(unsigned month, unsigned leap_year) {
    static const u8 days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    if (month < 1 || month > 12)
        month = 1;
    if (month == 2 && !(leap_year & 3))
        return 29;
    return days[month - 1];
}

static void set_time(MsxRtc *rtc, unsigned seconds, unsigned minutes,
                     unsigned hours, unsigned weekday, unsigned day,
                     unsigned month, unsigned year, unsigned leap_year) {
    encode_pair(rtc->registers[0], 0, seconds);
    encode_pair(rtc->registers[0], 2, minutes);
    encode_hour(rtc, hours);
    rtc->registers[0][6] = (u8)(weekday % 7);
    encode_pair(rtc->registers[0], 7, day);
    encode_pair(rtc->registers[0], 9, month);
    encode_pair(rtc->registers[0], 11, year);
    rtc->registers[1][11] = (u8)(leap_year & 3);
}

static void increment_second(MsxRtc *rtc) {
    unsigned seconds = decode_pair(rtc->registers[0], 0);
    unsigned minutes = decode_pair(rtc->registers[0], 2);
    unsigned hours = decode_hour(rtc);
    unsigned weekday = rtc->registers[0][6] % 7;
    unsigned day = decode_pair(rtc->registers[0], 7);
    unsigned month = decode_pair(rtc->registers[0], 9);
    unsigned year = decode_pair(rtc->registers[0], 11);
    unsigned leap_year = rtc->registers[1][11] & 3;

    if (day < 1)
        day = 1;
    if (month < 1 || month > 12)
        month = 1;
    if (++seconds >= 60) {
        seconds = 0;
        if (++minutes >= 60) {
            minutes = 0;
            if (++hours >= 24) {
                hours = 0;
                weekday = (weekday + 1) % 7;
                if (++day > days_in_month(month, leap_year)) {
                    day = 1;
                    if (++month > 12) {
                        month = 1;
                        year = (year + 1) % 100;
                        leap_year = (leap_year + 1) & 3;
                    }
                }
            }
        }
    }

    set_time(rtc, seconds, minutes, hours, weekday, day, month,
             year, leap_year);
}

static void initialize_host_time(MsxRtc *rtc) {
    time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    unsigned year;

    if (!local) {
        set_time(rtc, 0, 0, 0, 6, 1, 1, 3, 3);
        return;
    }
    year = (unsigned)((local->tm_year - 80 + 100) % 100);
    set_time(rtc, (unsigned)local->tm_sec, (unsigned)local->tm_min,
             (unsigned)local->tm_hour, (unsigned)local->tm_wday,
             (unsigned)local->tm_mday, (unsigned)local->tm_mon + 1,
             year, (unsigned)local->tm_year & 3);
}

void rtc_reset(MsxRtc *rtc) {
    if (!rtc)
        return;
    rtc->latch = 0;
    rtc->mode = RTC_MODE_TIMER_ENABLE;
    rtc->test = 0;
    rtc->reset = 0;
}

void rtc_init(MsxRtc *rtc) {
    if (!rtc)
        return;
    memset(rtc, 0, sizeof(*rtc));
    initialize_host_time(rtc);
    rtc_reset(rtc);
}

void rtc_advance(MsxRtc *rtc, unsigned cycles, unsigned cpu_hz) {
    if (!rtc || !cpu_hz || !(rtc->mode & RTC_MODE_TIMER_ENABLE))
        return;
    rtc->cycle_accumulator += cycles;
    while (rtc->cycle_accumulator >= cpu_hz) {
        rtc->cycle_accumulator -= cpu_hz;
        increment_second(rtc);
    }
}

void rtc_select(MsxRtc *rtc, u8 value) {
    if (rtc)
        rtc->latch = value & 0x0f;
}

u8 rtc_read_data(const MsxRtc *rtc) {
    unsigned block;

    if (!rtc)
        return 0x0f;
    switch (rtc->latch) {
        case RTC_MODE_REGISTER:
            return rtc->mode;
        case RTC_TEST_REGISTER:
        case RTC_RESET_REGISTER:
            return 0x0f;
        default:
            block = rtc->mode & RTC_MODE_BLOCK_MASK;
            return rtc->registers[block][rtc->latch] &
                   register_masks[block][rtc->latch];
    }
}

void rtc_write_data(MsxRtc *rtc, u8 value) {
    unsigned block;

    if (!rtc)
        return;
    value &= 0x0f;
    switch (rtc->latch) {
        case RTC_MODE_REGISTER:
            rtc->mode = value;
            break;
        case RTC_TEST_REGISTER:
            rtc->test = value;
            break;
        case RTC_RESET_REGISTER:
            rtc->reset = value;
            if (value & RTC_RESET_ALARM)
                memset(&rtc->registers[1][2], 0, 7);
            if (value & RTC_RESET_FRACTION)
                rtc->cycle_accumulator = 0;
            break;
        default:
            block = rtc->mode & RTC_MODE_BLOCK_MASK;
            rtc->registers[block][rtc->latch] =
                value & register_masks[block][rtc->latch];
            break;
    }
}
