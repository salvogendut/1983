bin_PROGRAMS = 1983

AM_LDFLAGS = $(WIN_STACK_LDFLAGS)

1983_SOURCES = \
	src/config.c \
	src/cartridge.c \
	src/cassette.c \
	src/ata.c \
	src/floppy.c \
	src/audio.c \
	src/display.c \
	src/gamepad.c \
	src/gifcap.c \
	src/ffmpeg_gif.c \
	src/kbd.c \
	src/leds.c \
	src/main.c \
	src/megaflash.c \
	src/models.c \
	src/msx.c \
	src/notify.c \
	src/overlay.c \
	src/paste.c \
	src/psg.c \
	src/rs232.c \
	src/rtc.c \
	src/scc.c \
	src/sdcard.c \
	src/sd_mapper.c \
	src/sunrise.c \
	src/ui.c \
	src/unapinet.c \
	src/vdp.c \
	src/wd2793.c \
	src/z80.c

noinst_HEADERS = \
	src/config.h \
	src/cartridge.h \
	src/cassette.h \
	src/ata.h \
	src/floppy.h \
	src/audio.h \
	src/display.h \
	src/gamepad.h \
	src/gifcap.h \
	src/ffmpeg_gif.h \
	src/kbd.h \
	src/leds.h \
	src/megaflash.h \
	src/models.h \
	src/msx.h \
	src/notify.h \
	src/overlay.h \
	src/paste.h \
	src/psg.h \
	src/rs232.h \
	src/rtc.h \
	src/scc.h \
	src/sdcard.h \
	src/sd_mapper.h \
	src/sunrise.h \
	src/shutter_wav.h \
	src/types.h \
	src/ui.h \
	src/unapinet.h \
	src/vdp.h \
	src/wd2793.h \
	src/z80.h

1983_CPPFLAGS = -DPKGDATADIR=\"$(pkgdatadir)\"
1983_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic \
	-DPROG_GIT_COMMIT=\"$(PROG_GIT_COMMIT)\"
1983_LDADD = $(SDL3_LIBS) $(NETWORK_LIBS) -lm $(WIN_RC_OBJ)
1983_DEPENDENCIES = $(WIN_RC_OBJ)

icons/1983-rc.o: $(srcdir)/icons/1983.rc $(srcdir)/icons/1983.ico
	@mkdir -p icons
	$(WINDRES) -I$(srcdir)/icons -i $< -O coff -o $@

CLEANFILES = icons/1983-rc.o

check_PROGRAMS = test-cartridge test-cassette test-ata test-floppy test-wd2793 test-sunrise test-sdcard test-sd-mapper test-megaflash test-unapinet test-config test-models test-msx test-gamepad test-kbd test-paste test-overlay test-vdp test-psg test-scc test-rtc test-gifcap test-rs232
TESTS = $(check_PROGRAMS) diagnostics/test-cli.sh
test_cartridge_SOURCES = diagnostics/test_cartridge.c src/cartridge.c src/scc.c
test_cartridge_CPPFLAGS = -I$(top_srcdir)/src
test_cartridge_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_cassette_SOURCES = diagnostics/test_cassette.c src/cassette.c
test_cassette_CPPFLAGS = -I$(top_srcdir)/src
test_cassette_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_ata_SOURCES = diagnostics/test_ata.c src/ata.c
test_ata_CPPFLAGS = -I$(top_srcdir)/src
test_ata_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_floppy_SOURCES = diagnostics/test_floppy.c src/floppy.c
test_floppy_CPPFLAGS = -I$(top_srcdir)/src
test_floppy_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_wd2793_SOURCES = diagnostics/test_wd2793.c src/floppy.c src/wd2793.c
test_wd2793_CPPFLAGS = -I$(top_srcdir)/src
test_wd2793_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_sunrise_SOURCES = diagnostics/test_sunrise.c src/ata.c src/sunrise.c
test_sunrise_CPPFLAGS = -I$(top_srcdir)/src
test_sunrise_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_sdcard_SOURCES = diagnostics/test_sdcard.c src/sdcard.c
test_sdcard_CPPFLAGS = -I$(top_srcdir)/src
test_sdcard_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_sd_mapper_SOURCES = diagnostics/test_sd_mapper.c src/sdcard.c src/sd_mapper.c
test_sd_mapper_CPPFLAGS = -I$(top_srcdir)/src
test_sd_mapper_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_megaflash_SOURCES = diagnostics/test_megaflash.c src/megaflash.c src/psg.c src/scc.c src/sdcard.c
test_megaflash_CPPFLAGS = -I$(top_srcdir)/src
test_megaflash_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_unapinet_SOURCES = diagnostics/test_unapinet.c src/unapinet.c
test_unapinet_CPPFLAGS = -I$(top_srcdir)/src
test_unapinet_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_unapinet_LDADD = $(SDL3_LIBS) $(NETWORK_LIBS)

test_config_SOURCES = diagnostics/test_config.c src/config.c src/cartridge.c src/cassette.c src/ata.c src/floppy.c src/megaflash.c src/msx.c src/psg.c src/rtc.c src/scc.c src/sdcard.c src/sd_mapper.c src/sunrise.c src/vdp.c src/wd2793.c src/z80.c
test_config_CPPFLAGS = -I$(top_srcdir)/src
test_config_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic

test_models_SOURCES = diagnostics/test_models.c src/models.c src/cartridge.c src/cassette.c src/ata.c src/floppy.c src/megaflash.c src/msx.c src/psg.c src/rtc.c src/scc.c src/sdcard.c src/sd_mapper.c src/sunrise.c src/vdp.c src/wd2793.c src/z80.c
test_models_CPPFLAGS = -I$(top_srcdir)/src
test_models_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_msx_SOURCES = diagnostics/test_msx.c src/cartridge.c src/cassette.c src/ata.c src/floppy.c src/megaflash.c src/msx.c src/psg.c src/rtc.c src/scc.c src/sdcard.c src/sd_mapper.c src/sunrise.c src/vdp.c src/wd2793.c src/z80.c
test_msx_CPPFLAGS = -I$(top_srcdir)/src
test_msx_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_gamepad_SOURCES = diagnostics/test_gamepad.c src/gamepad.c
test_gamepad_CPPFLAGS = -I$(top_srcdir)/src
test_gamepad_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_gamepad_LDADD = $(SDL3_LIBS)

test_kbd_SOURCES = diagnostics/test_kbd.c src/cartridge.c src/cassette.c src/ata.c src/floppy.c src/kbd.c src/megaflash.c src/msx.c src/psg.c src/rtc.c src/scc.c src/sdcard.c src/sd_mapper.c src/sunrise.c src/vdp.c src/wd2793.c src/z80.c
test_kbd_CPPFLAGS = -I$(top_srcdir)/src
test_kbd_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_kbd_LDADD = $(SDL3_LIBS)

test_paste_SOURCES = diagnostics/test_paste.c src/cartridge.c src/cassette.c src/ata.c src/floppy.c src/megaflash.c src/msx.c src/paste.c src/psg.c src/rtc.c src/scc.c src/sdcard.c src/sd_mapper.c src/sunrise.c src/vdp.c src/wd2793.c src/z80.c
test_paste_CPPFLAGS = -I$(top_srcdir)/src
test_paste_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_overlay_SOURCES = diagnostics/test_overlay.c src/config.c src/cartridge.c src/cassette.c src/ata.c src/floppy.c src/display.c src/leds.c src/megaflash.c src/models.c src/msx.c src/notify.c src/overlay.c src/psg.c src/rtc.c src/scc.c src/sdcard.c src/sd_mapper.c src/sunrise.c src/ui.c src/unapinet.c src/vdp.c src/wd2793.c src/z80.c
test_overlay_CPPFLAGS = -I$(top_srcdir)/src
test_overlay_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra -Wpedantic
test_overlay_LDADD = $(SDL3_LIBS) $(NETWORK_LIBS) -lm

test_vdp_SOURCES = diagnostics/test_vdp.c src/vdp.c
test_vdp_CPPFLAGS = -I$(top_srcdir)/src
test_vdp_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_psg_SOURCES = diagnostics/test_psg.c src/psg.c
test_psg_CPPFLAGS = -I$(top_srcdir)/src
test_psg_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_scc_SOURCES = diagnostics/test_scc.c src/scc.c
test_scc_CPPFLAGS = -I$(top_srcdir)/src
test_scc_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_rtc_SOURCES = diagnostics/test_rtc.c src/rtc.c
test_rtc_CPPFLAGS = -I$(top_srcdir)/src
test_rtc_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_gifcap_SOURCES = diagnostics/test_gifcap.c src/gifcap.c
test_gifcap_CPPFLAGS = -I$(top_srcdir)/src
test_gifcap_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

test_rs232_SOURCES = diagnostics/test_rs232.c src/rs232.c
test_rs232_CPPFLAGS = -I$(top_srcdir)/src
test_rs232_CFLAGS = $(AM_CFLAGS) -Wall -Wextra -Wpedantic

desktopdir         = $(datadir)/applications
dist_desktop_DATA  = io.github.salvogendut.Emulator1983.desktop

metainfodir        = $(datadir)/metainfo
dist_metainfo_DATA = io.github.salvogendut.Emulator1983.metainfo.xml

dist_pkgdata_DATA = 1983-models.conf

cbiosdir = $(pkgdatadir)/ROMS
dist_cbios_DATA = \
	ROMS/README.C-BIOS \
	ROMS/cbios_logo_msx1.rom \
	ROMS/cbios_main_msx1.rom

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
	USAGE.md \
	CONTROLS.md \
	CONFIGURATION.md \
	1983.png \
	sample-rainbios-nextor-geobench-on-1983-20260802-165621.gif \
	TECHNICAL.md \
	DEVELOPMENT.md \
	ROADMAP.md \
	BOOT_TARGETS.md \
	INSTALL.md \
	1983.conf.example \
	1983.spec \
	io.github.salvogendut.Emulator1983.yml \
	.github/workflows/build.yml \
	packaging/macos/package.sh \
	diagnostics/test-cli.sh \
	icons/1983.png \
	icons/README.md \
	icons/1983.rc \
	icons/1983.ico
