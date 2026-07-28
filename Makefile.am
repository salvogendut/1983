bin_PROGRAMS = 1983

1983_SOURCES = \
	src/config.c \
	src/display.c \
	src/kbd.c \
	src/leds.c \
	src/main.c \
	src/msx.c \
	src/notify.c \
	src/overlay.c \
	src/ui.c \
	src/vdp.c \
	src/z80.c

noinst_HEADERS = \
	src/config.h \
	src/display.h \
	src/kbd.h \
	src/leds.h \
	src/msx.h \
	src/notify.h \
	src/overlay.h \
	src/types.h \
	src/ui.h \
	src/vdp.h \
	src/z80.h

1983_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic \
	-DPROG_GIT_COMMIT=\"$(PROG_GIT_COMMIT)\"
1983_LDADD = $(SDL3_LIBS) -lm $(WIN_RC_OBJ)
1983_DEPENDENCIES = $(WIN_RC_OBJ)

icons/1983-rc.o: $(srcdir)/icons/1983.rc $(srcdir)/icons/1983.ico
	@mkdir -p icons
	$(WINDRES) -I$(srcdir)/icons -i $< -O coff -o $@

CLEANFILES = icons/1983-rc.o

check_PROGRAMS = test-msx test-kbd test-vdp
TESTS = $(check_PROGRAMS)
test_msx_SOURCES = tests/test_msx.c src/msx.c src/vdp.c src/z80.c
test_msx_CPPFLAGS = -I$(top_srcdir)/src
test_msx_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_kbd_SOURCES = tests/test_kbd.c src/kbd.c src/msx.c src/vdp.c src/z80.c
test_kbd_CPPFLAGS = -I$(top_srcdir)/src
test_kbd_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_kbd_LDADD = $(SDL3_LIBS)

test_vdp_SOURCES = tests/test_vdp.c src/vdp.c
test_vdp_CPPFLAGS = -I$(top_srcdir)/src
test_vdp_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

desktopdir         = $(datadir)/applications
dist_desktop_DATA  = io.github.salvogendut.Emulator1983.desktop

metainfodir        = $(datadir)/metainfo
dist_metainfo_DATA = io.github.salvogendut.Emulator1983.metainfo.xml

icon16dir        = $(datadir)/icons/hicolor/16x16/apps
dist_icon16_DATA = icons/16x16/apps/io.github.salvogendut.Emulator1983.png
icon32dir        = $(datadir)/icons/hicolor/32x32/apps
dist_icon32_DATA = icons/32x32/apps/io.github.salvogendut.Emulator1983.png
icon48dir        = $(datadir)/icons/hicolor/48x48/apps
dist_icon48_DATA = icons/48x48/apps/io.github.salvogendut.Emulator1983.png
icon64dir        = $(datadir)/icons/hicolor/64x64/apps
dist_icon64_DATA = icons/64x64/apps/io.github.salvogendut.Emulator1983.png
icon128dir        = $(datadir)/icons/hicolor/128x128/apps
dist_icon128_DATA = icons/128x128/apps/io.github.salvogendut.Emulator1983.png
icon256dir        = $(datadir)/icons/hicolor/256x256/apps
dist_icon256_DATA = icons/256x256/apps/io.github.salvogendut.Emulator1983.png
icon512dir        = $(datadir)/icons/hicolor/512x512/apps
dist_icon512_DATA = icons/512x512/apps/io.github.salvogendut.Emulator1983.png

EXTRA_DIST = \
	LICENSE \
	README.md \
	DEVELOPMENT.md \
	BOOT_TARGETS.md \
	1983.conf.example \
	icons/1983.png \
	icons/README.md \
	icons/1983.rc \
	icons/1983.ico
