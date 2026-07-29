#define _POSIX_C_SOURCE 200809L

#include "rtc.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define RTC_FILENO _fileno
#define RTC_MKDIR(path) _mkdir(path)
#define RTC_SYNC _commit
#else
#include <sys/stat.h>
#include <unistd.h>
#define RTC_FILENO fileno
#define RTC_MKDIR(path) mkdir((path), 0755)
#define RTC_SYNC fsync
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    RTC_MODE_REGISTER = 13,
    RTC_TEST_REGISTER = 14,
    RTC_RESET_REGISTER = 15,
    RTC_MODE_BLOCK_MASK = 0x03,
    RTC_MODE_TIMER_ENABLE = 0x08,
    RTC_TEST_SECONDS = 0x01,
    RTC_TEST_MINUTES = 0x02,
    RTC_TEST_DAYS = 0x04,
    RTC_TEST_YEARS = 0x08,
    RTC_RESET_ALARM = 0x01,
    RTC_RESET_FRACTION = 0x02,
    RTC_TEST_HZ = 16384,
    RTC_PERSISTENCE_VERSION = 1,
    RTC_PERSISTENCE_PAYLOAD_OFFSET = 28,
    RTC_PERSISTENCE_CHECKSUM_OFFSET = 80,
    RTC_PERSISTENCE_FLAG_TIMER_RUNNING = 0x01,
};

#define RTC_MAX_OFFLINE_SECONDS (200ULL * 366ULL * 24ULL * 60ULL * 60ULL)

static const u8 persistence_magic[8] = {
    '1', '9', '8', '3', 'R', 'T', 'C', '\0'
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

static void set_error(char *error, size_t error_size,
                      const char *format, ...) {
    va_list arguments;

    if (!error || !error_size)
        return;
    va_start(arguments, format);
    vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static void clear_error(char *error, size_t error_size) {
    if (error && error_size)
        error[0] = '\0';
}

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

static void advance_time(MsxRtc *rtc, u64 add_seconds,
                         u64 add_minutes, u64 add_days,
                         u64 add_years) {
    u64 seconds = decode_pair(rtc->registers[0], 0);
    u64 minutes = decode_pair(rtc->registers[0], 2);
    u64 hours = decode_hour(rtc);
    unsigned weekday = rtc->registers[0][6] % 7;
    unsigned day = decode_pair(rtc->registers[0], 7);
    unsigned month = decode_pair(rtc->registers[0], 9);
    unsigned year = decode_pair(rtc->registers[0], 11);
    unsigned leap_year = rtc->registers[1][11] & 3;
    u64 carry;

    if (day < 1)
        day = 1;
    if (month < 1 || month > 12)
        month = 1;

    seconds += add_seconds;
    carry = seconds / 60;
    seconds %= 60;
    minutes += add_minutes + carry;
    carry = minutes / 60;
    minutes %= 60;
    hours += carry;
    add_days += hours / 24;
    hours %= 24;

    if (add_days)
        weekday = (unsigned)((weekday + add_days) % 7);
    while (add_days) {
        unsigned remaining =
            days_in_month(month, leap_year) - day;

        if (add_days <= remaining) {
            day += (unsigned)add_days;
            add_days = 0;
            break;
        }
        add_days -= (u64)remaining + 1;
        day = 1;
        if (++month > 12) {
            month = 1;
            year = (year + 1) % 100;
            leap_year = (leap_year + 1) & 3;
        }
    }
    if (add_years) {
        year = (unsigned)((year + add_years) % 100);
        leap_year = (unsigned)((leap_year + add_years) & 3);
    }

    set_time(rtc, (unsigned)seconds, (unsigned)minutes,
             (unsigned)hours, weekday, day, month, year, leap_year);
    rtc->dirty = true;
}

void rtc_advance_seconds(MsxRtc *rtc, u64 seconds) {
    if (!rtc || !seconds)
        return;
    advance_time(rtc, seconds, 0, 0, 0);
}

static void initialize_host_time_at(MsxRtc *rtc, u64 host_seconds) {
    time_t now = (time_t)host_seconds;
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

u64 rtc_host_seconds(void) {
    time_t now = time(NULL);

    return now < 0 ? 0 : (u64)now;
}

void rtc_reset(MsxRtc *rtc) {
    if (!rtc)
        return;
    rtc->latch = 0;
    rtc->mode = RTC_MODE_TIMER_ENABLE;
    rtc->test = 0;
    rtc->reset = 0;
    rtc->test_cycle_accumulator = 0;
    rtc->dirty = true;
}

void rtc_init_at(MsxRtc *rtc, u64 host_seconds) {
    if (!rtc)
        return;
    memset(rtc, 0, sizeof(*rtc));
    initialize_host_time_at(rtc, host_seconds);
    rtc_reset(rtc);
    rtc->dirty = false;
}

void rtc_init(MsxRtc *rtc) {
    rtc_init_at(rtc, rtc_host_seconds());
}

void rtc_advance(MsxRtc *rtc, unsigned cycles, unsigned cpu_hz) {
    u64 elapsed_test_ticks;

    if (!rtc || !cpu_hz || !cycles)
        return;
    if (rtc->mode & RTC_MODE_TIMER_ENABLE) {
        rtc->cycle_accumulator += cycles;
        if (rtc->cycle_accumulator >= cpu_hz) {
            u64 elapsed_seconds = rtc->cycle_accumulator / cpu_hz;

            rtc->cycle_accumulator %= cpu_hz;
            rtc_advance_seconds(rtc, elapsed_seconds);
        }
    }
    if (!rtc->test)
        return;
    rtc->test_cycle_accumulator +=
        (u64)cycles * RTC_TEST_HZ;
    elapsed_test_ticks =
        rtc->test_cycle_accumulator / cpu_hz;
    rtc->test_cycle_accumulator %= cpu_hz;
    if (!elapsed_test_ticks)
        return;
    advance_time(
        rtc,
        (rtc->test & RTC_TEST_SECONDS) ? elapsed_test_ticks : 0,
        (rtc->test & RTC_TEST_MINUTES) ? elapsed_test_ticks : 0,
        (rtc->test & RTC_TEST_DAYS) ? elapsed_test_ticks : 0,
        (rtc->test & RTC_TEST_YEARS) ? elapsed_test_ticks : 0);
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
            if (rtc->mode != value)
                rtc->dirty = true;
            rtc->mode = value;
            break;
        case RTC_TEST_REGISTER:
            if (rtc->test != value)
                rtc->test_cycle_accumulator = 0;
            rtc->test = value;
            break;
        case RTC_RESET_REGISTER:
            rtc->reset = value;
            if (value & RTC_RESET_ALARM) {
                memset(&rtc->registers[1][2], 0, 7);
                rtc->dirty = true;
            }
            if (value & RTC_RESET_FRACTION)
                rtc->cycle_accumulator = 0;
            break;
        default:
            block = rtc->mode & RTC_MODE_BLOCK_MASK;
            value &= register_masks[block][rtc->latch];
            if (rtc->registers[block][rtc->latch] != value)
                rtc->dirty = true;
            rtc->registers[block][rtc->latch] = value;
            break;
    }
}

bool rtc_dirty(const MsxRtc *rtc) {
    return rtc && rtc->dirty;
}

static void write_le32(u8 *data, u32 value) {
    data[0] = (u8)value;
    data[1] = (u8)(value >> 8);
    data[2] = (u8)(value >> 16);
    data[3] = (u8)(value >> 24);
}

static u32 read_le32(const u8 *data) {
    return (u32)data[0] |
           ((u32)data[1] << 8) |
           ((u32)data[2] << 16) |
           ((u32)data[3] << 24);
}

static void write_le64(u8 *data, u64 value) {
    for (unsigned i = 0; i < 8; ++i)
        data[i] = (u8)(value >> (8 * i));
}

static u64 read_le64(const u8 *data) {
    u64 value = 0;

    for (unsigned i = 0; i < 8; ++i)
        value |= (u64)data[i] << (8 * i);
    return value;
}

static u32 persistence_checksum(const u8 *data, size_t size) {
    u32 hash = 2166136261u;

    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool valid_bcd_pair(const u8 *registers, unsigned low,
                           unsigned maximum) {
    return registers[low] <= 9 &&
           registers[low + 1] <= 9 &&
           decode_pair(registers, low) <= maximum;
}

static bool valid_persisted_registers(const MsxRtc *rtc) {
    unsigned raw_hour = decode_pair(rtc->registers[0], 4);
    unsigned day = decode_pair(rtc->registers[0], 7);
    unsigned month = decode_pair(rtc->registers[0], 9);

    for (unsigned block = 0; block < MSX_RTC_BLOCK_COUNT; ++block) {
        for (unsigned reg = 0; reg < MSX_RTC_BLOCK_SIZE; ++reg) {
            if (rtc->registers[block][reg] &
                (u8)~register_masks[block][reg])
                return false;
        }
    }
    if (!valid_bcd_pair(rtc->registers[0], 0, 59) ||
        !valid_bcd_pair(rtc->registers[0], 2, 59) ||
        rtc->registers[0][4] > 9 ||
        rtc->registers[0][6] >= 7 ||
        !valid_bcd_pair(rtc->registers[0], 7, 31) ||
        !valid_bcd_pair(rtc->registers[0], 9, 12) ||
        !valid_bcd_pair(rtc->registers[0], 11, 99) ||
        day < 1 || month < 1 ||
        day > days_in_month(month, rtc->registers[1][11]))
        return false;
    if (twenty_four_hour_mode(rtc))
        return raw_hour < 24;
    return raw_hour < 12 ||
           (raw_hour >= 20 && raw_hour < 32);
}

static void ensure_parent(const char *path) {
    char copy[PATH_MAX];
    char *cursor;

    snprintf(copy, sizeof(copy), "%s", path);
    cursor = strrchr(copy, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(copy, '\\');
        if (!cursor || (backslash && backslash > cursor))
            cursor = backslash;
    }
#endif
    if (!cursor)
        return;
    *cursor = '\0';
    cursor = copy;
    if (*cursor == '/')
        ++cursor;
#ifdef _WIN32
    if (cursor[0] && cursor[1] == ':')
        cursor += 2;
#endif
    while ((cursor = strpbrk(cursor, "/\\")) != NULL) {
        char saved = *cursor;

        *cursor = '\0';
        if (copy[0])
            RTC_MKDIR(copy);
        *cursor = saved;
        ++cursor;
    }
    if (copy[0])
        RTC_MKDIR(copy);
}

int rtc_load_persistence(MsxRtc *rtc, const char *path,
                         u64 host_seconds,
                         char *error, size_t error_size) {
    u8 data[MSX_RTC_PERSISTENCE_SIZE];
    MsxRtc loaded;
    FILE *file;
    u64 saved_seconds;
    u32 flags;

    clear_error(error, error_size);
    if (!rtc || !path || !path[0]) {
        set_error(error, error_size, "Invalid RTC persistence path");
        return -1;
    }
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT)
            return 1;
        set_error(error, error_size, "Cannot open RTC CMOS: %s",
                  strerror(errno));
        return -1;
    }
    if (fread(data, 1, sizeof(data), file) != sizeof(data) ||
        fgetc(file) != EOF) {
        bool io_error = ferror(file);

        fclose(file);
        if (io_error)
            set_error(error, error_size,
                      "Cannot read RTC CMOS: %s", strerror(errno));
        else
            set_error(error, error_size,
                      "Invalid RTC CMOS file size");
        return -1;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, "Cannot close RTC CMOS: %s",
                  strerror(errno));
        return -1;
    }
    if (memcmp(data, persistence_magic, sizeof(persistence_magic)) != 0 ||
        read_le32(data + 8) != RTC_PERSISTENCE_VERSION ||
        read_le32(data + 12) != MSX_RTC_BLOCK_COUNT *
                                  MSX_RTC_BLOCK_SIZE ||
        read_le32(data + RTC_PERSISTENCE_CHECKSUM_OFFSET) !=
            persistence_checksum(
                data, RTC_PERSISTENCE_CHECKSUM_OFFSET)) {
        set_error(error, error_size,
                  "Invalid or corrupted RTC CMOS file");
        return -1;
    }

    loaded = *rtc;
    memcpy(loaded.registers,
           data + RTC_PERSISTENCE_PAYLOAD_OFFSET,
           sizeof(loaded.registers));
    if (!valid_persisted_registers(&loaded)) {
        set_error(error, error_size,
                  "RTC CMOS contains invalid register values");
        return -1;
    }
    saved_seconds = read_le64(data + 16);
    flags = read_le32(data + 24);
    if (flags & ~RTC_PERSISTENCE_FLAG_TIMER_RUNNING) {
        set_error(error, error_size,
                  "RTC CMOS contains unsupported flags");
        return -1;
    }
    if (host_seconds > saved_seconds &&
        host_seconds - saved_seconds > RTC_MAX_OFFLINE_SECONDS) {
        set_error(error, error_size,
                  "RTC CMOS timestamp is outside the supported range");
        return -1;
    }
    loaded.dirty = false;
    if ((flags & RTC_PERSISTENCE_FLAG_TIMER_RUNNING) &&
        host_seconds > saved_seconds)
        rtc_advance_seconds(&loaded, host_seconds - saved_seconds);
    *rtc = loaded;
    return 0;
}

int rtc_save_persistence(MsxRtc *rtc, const char *path,
                         u64 host_seconds,
                         char *error, size_t error_size) {
    u8 data[MSX_RTC_PERSISTENCE_SIZE] = { 0 };
    char temporary[PATH_MAX];
    FILE *file;
    bool failed = false;

    clear_error(error, error_size);
    if (!rtc || !path || !path[0] ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
            (int)sizeof(temporary)) {
        set_error(error, error_size, "Invalid RTC persistence path");
        return -1;
    }
    memcpy(data, persistence_magic, sizeof(persistence_magic));
    write_le32(data + 8, RTC_PERSISTENCE_VERSION);
    write_le32(data + 12,
               MSX_RTC_BLOCK_COUNT * MSX_RTC_BLOCK_SIZE);
    write_le64(data + 16, host_seconds);
    write_le32(data + 24,
               (rtc->mode & RTC_MODE_TIMER_ENABLE)
               ? RTC_PERSISTENCE_FLAG_TIMER_RUNNING : 0);
    memcpy(data + RTC_PERSISTENCE_PAYLOAD_OFFSET,
           rtc->registers, sizeof(rtc->registers));
    write_le32(data + RTC_PERSISTENCE_CHECKSUM_OFFSET,
               persistence_checksum(
                   data, RTC_PERSISTENCE_CHECKSUM_OFFSET));

    ensure_parent(path);
    file = fopen(temporary, "wb");
    if (!file) {
        set_error(error, error_size, "Cannot create RTC CMOS: %s",
                  strerror(errno));
        return -1;
    }
    if (fwrite(data, 1, sizeof(data), file) != sizeof(data) ||
        fflush(file) != 0 ||
        RTC_SYNC(RTC_FILENO(file)) != 0) {
        set_error(error, error_size, "Cannot flush RTC CMOS: %s",
                  strerror(errno));
        failed = true;
    }
    if (fclose(file) != 0) {
        if (!failed)
            set_error(error, error_size, "Cannot close RTC CMOS: %s",
                      strerror(errno));
        failed = true;
    }
    if (failed) {
        remove(temporary);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING |
                     MOVEFILE_WRITE_THROUGH)) {
        remove(temporary);
        set_error(error, error_size,
                  "Cannot replace RTC CMOS file");
        return -1;
    }
#else
    if (rename(temporary, path) != 0) {
        set_error(error, error_size, "Cannot replace RTC CMOS: %s",
                  strerror(errno));
        remove(temporary);
        return -1;
    }
#endif
    rtc->dirty = false;
    return 0;
}
