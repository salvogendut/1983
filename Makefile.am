bin_PROGRAMS = 1983

1983_SOURCES = \
	src/config.c \
	src/cartridge.c \
	src/audio.c \
	src/display.c \
	src/kbd.c \
	src/leds.c \
	src/main.c \
	src/models.c \
	src/msx.c \
	src/notify.c \
	src/overlay.c \
	src/psg.c \
	src/rtc.c \
	src/ui.c \
	src/vdp.c \
	src/z80.c

noinst_HEADERS = \
	src/config.h \
	src/cartridge.h \
	src/audio.h \
	src/display.h \
	src/kbd.h \
	src/leds.h \
	src/models.h \
	src/msx.h \
	src/notify.h \
	src/overlay.h \
	src/psg.h \
	src/rtc.h \
	src/types.h \
	src/ui.h \
	src/vdp.h \
	src/z80.h

1983_CPPFLAGS = -DPKGDATADIR=\"$(pkgdatadir)\"
1983_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic \
	-DPROG_GIT_COMMIT=\"$(PROG_GIT_COMMIT)\"
1983_LDADD = $(SDL3_LIBS) -lm $(WIN_RC_OBJ)
1983_DEPENDENCIES = $(WIN_RC_OBJ)

icons/1983-rc.o: $(srcdir)/icons/1983.rc $(srcdir)/icons/1983.ico
	@mkdir -p icons
	$(WINDRES) -I$(srcdir)/icons -i $< -O coff -o $@

CLEANFILES = icons/1983-rc.o

check_PROGRAMS = test-cartridge test-config test-models test-msx test-kbd test-overlay test-vdp test-psg test-rtc
TESTS = $(check_PROGRAMS) tests/test-cli.sh
test_cartridge_SOURCES = tests/test_cartridge.c src/cartridge.c
test_cartridge_CPPFLAGS = -I$(top_srcdir)/src
test_cartridge_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_config_SOURCES = tests/test_config.c src/config.c src/cartridge.c src/msx.c src/psg.c src/rtc.c src/vdp.c src/z80.c
test_config_CPPFLAGS = -I$(top_srcdir)/src
test_config_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_models_SOURCES = tests/test_models.c src/models.c src/cartridge.c src/msx.c src/psg.c src/rtc.c src/vdp.c src/z80.c
test_models_CPPFLAGS = -I$(top_srcdir)/src
test_models_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_msx_SOURCES = tests/test_msx.c src/cartridge.c src/msx.c src/psg.c src/rtc.c src/vdp.c src/z80.c
test_msx_CPPFLAGS = -I$(top_srcdir)/src
test_msx_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_kbd_SOURCES = tests/test_kbd.c src/cartridge.c src/kbd.c src/msx.c src/psg.c src/rtc.c src/vdp.c src/z80.c
test_kbd_CPPFLAGS = -I$(top_srcdir)/src
test_kbd_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_kbd_LDADD = $(SDL3_LIBS)

test_overlay_SOURCES = tests/test_overlay.c src/config.c src/cartridge.c src/display.c src/leds.c src/models.c src/msx.c src/notify.c src/overlay.c src/psg.c src/rtc.c src/ui.c src/vdp.c src/z80.c
test_overlay_CPPFLAGS = -I$(top_srcdir)/src
test_overlay_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_overlay_LDADD = $(SDL3_LIBS) -lm

test_vdp_SOURCES = tests/test_vdp.c src/vdp.c
test_vdp_CPPFLAGS = -I$(top_srcdir)/src
test_vdp_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_psg_SOURCES = tests/test_psg.c src/psg.c
test_psg_CPPFLAGS = -I$(top_srcdir)/src
test_psg_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_rtc_SOURCES = tests/test_rtc.c src/rtc.c
test_rtc_CPPFLAGS = -I$(top_srcdir)/src
test_rtc_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

desktopdir         = $(datadir)/applications
dist_desktop_DATA  = io.github.salvogendut.Emulator1983.desktop

metainfodir        = $(datadir)/metainfo
dist_metainfo_DATA = io.github.salvogendut.Emulator1983.metainfo.xml

dist_pkgdata_DATA = 1983-models.conf

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
	tests/test-cli.sh \
	DOS/NEXTOR.SYS \
	icons/1983.png \
	icons/README.md \
	icons/1983.rc \
	icons/1983.ico
