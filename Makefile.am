bin_PROGRAMS = 1983

1983_SOURCES = \
	src/config.c \
	src/display.c \
	src/leds.c \
	src/main.c \
	src/msx.c \
	src/notify.c \
	src/overlay.c \
	src/ui.c

noinst_HEADERS = \
	src/config.h \
	src/display.h \
	src/leds.h \
	src/msx.h \
	src/notify.h \
	src/overlay.h \
	src/types.h \
	src/ui.h

1983_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic \
	-DPROG_GIT_COMMIT=\"$(PROG_GIT_COMMIT)\"
1983_LDADD = $(SDL3_LIBS) -lm

check_PROGRAMS = test-msx
TESTS = $(check_PROGRAMS)
test_msx_SOURCES = tests/test_msx.c src/msx.c
test_msx_CPPFLAGS = -I$(top_srcdir)/src
test_msx_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

EXTRA_DIST = \
	LICENSE \
	README.md \
	DEVELOPMENT.md \
	BOOT_TARGETS.md \
	1983.conf.example
