# WASM browser build for the 1983 core (mirrors 1984's JS1984 POC).
#
# Compiles a reduced source set (the machine core + keyboard + paste) plus web
# stubs for host-only modules, with Emscripten. Run inside the my-distrobox
# container where emcc lives:
#
#   export PATH=/var/home/salvogendut/emsdk/upstream/emscripten:$PATH
#   make clean && make
#   python3 -m http.server 8080 --directory dist
#
# Produces a self-contained publish directory under dist/. Compiler
# intermediates stay under build/.

EMCC      = emcc
BUILD_DIR = build
DIST_DIR  = dist
UNAPI_HOST ?= 127.0.0.1
UNAPI_PORT ?= 1983
CFLAGS    = -O2 -std=c11 -Wall -Wextra -I../src -Icompat -I.

SRCS = \
	../src/z80.c \
	../src/vdp.c \
	../src/v9990.c \
	../src/psg.c \
	../src/msx.c \
	../src/cartridge.c \
	../src/cassette.c \
	../src/ata.c \
	../src/scsi_disk.c \
	../src/msx_scsi.c \
	../src/floppy.c \
	../src/wd2793.c \
	../src/tc8566.c \
	../src/megaflash.c \
	../src/rtc.c \
	../src/scc.c \
	../src/sdcard.c \
	../src/sd_mapper.c \
	../src/sunrise.c \
	../src/kbd.c \
	../src/paste.c

CORE_OBJS = $(patsubst ../src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJS = $(CORE_OBJS) $(BUILD_DIR)/poc_main.o $(BUILD_DIR)/stubs.o \
	$(BUILD_DIR)/unapinet_wasm.o $(BUILD_DIR)/unapinet_web.o
WASM_ASSETS = $(DIST_DIR)/1983.js $(DIST_DIR)/1983.wasm
BRAND_ICON = ../icons/512x512/apps/io.github.salvogendut.Emulator1983.png
STYLE_ASSETS = styles.css theme-sonyhb-f1xd.css theme-retro-crt.css \
	theme-sapporo.css theme-sapporo-dark.css
FIRMWARE_ASSETS = \
	../ROMS/cbios_main_msx1.rom \
	../ROMS/cbios_logo_msx1.rom \
	../ROMS/rainbios_msx2.rom \
	../ROMS/rainbios_msx2_sub.rom \
	../ROMS/rainbios_omega.rom \
	../ROMS/rainbios_nms8250_disk.rom \
	../ROMS/Nextor-2.1.1.SunriseIDE.ROM \
	../ROMS/SDM\ V2\ Nextor2.1.1.rom \
	../ROMS/BertSCSI-v1-D0h-D7h.ROM \
	../ROMS/BertSCSI-v2-30h-37h.ROM

RUNTIME_ASSETS = $(addprefix $(DIST_DIR)/,$(STYLE_ASSETS)) $(DIST_DIR)/app.js \
	$(DIST_DIR)/audio-scheduler.js $(DIST_DIR)/gamepad.js $(DIST_DIR)/hardware-ui.js \
	$(DIST_DIR)/media-url.js $(DIST_DIR)/unapi-relay-protocol.js \
	$(DIST_DIR)/unapi-bridge.js $(DIST_DIR)/nextor-license.txt \
	$(DIST_DIR)/brand-1983.png $(WASM_ASSETS)

all: $(DIST_DIR)/index.html $(RUNTIME_ASSETS)

test: test-audio-scheduler test-gamepad test-hardware-ui test-media-url test-theme \
	test-ui-contract test-unapi

test-audio-scheduler:
	node test_audio_scheduler.js

test-gamepad:
	node test_gamepad.js

test-hardware-ui:
	node test_hardware_ui.js

test-media-url:
	node test_media_url.js

test-theme:
	node test_theme.js

test-ui-contract:
	node test_ui_contract.js

test-unapi:
	node test_unapi_protocol.js
	node test_unapi_bridge.js

test-relay:
	npm --prefix relay test

check-wasm: all
	node test_wasm_machine.js

check-wasm-symbos: all
	test -n "$(SYMBOS_IMAGE)"
	node test_wasm_symbos.js "$(SYMBOS_IMAGE)" \
	    /tmp/1983-wasm-symbos.ppm 5000 "$(or $(SYMBOS_CONTROLLER),sunrise)"

check-wasm-scsi: all
	test -n "$(SCSI_ROM)"
	test -n "$(SCSI_IMAGE)"
	node test_wasm_scsi.js "$(SCSI_ROM)" "$(SCSI_IMAGE)" 1500 \
	    "$(SCSI_UNIFIED_ROM)"

$(WASM_ASSETS) &: $(OBJS) $(FIRMWARE_ASSETS) | $(DIST_DIR)
	$(EMCC) $(CFLAGS) $(OBJS) -o $(DIST_DIR)/1983.js \
	    -sMODULARIZE -sEXPORT_NAME=create1983 \
	    -sEXPORTED_FUNCTIONS='_poc_init,_poc_init_model,_poc_set_video_standard,_poc_video_standard,_poc_ram_kb,_poc_set_ram_kb,_poc_load_cartridge,_poc_load_cartridge_slot,_poc_eject_cartridge,_poc_cartridge_loaded,_poc_reset,_poc_omega_unified_bank,_poc_install_omega_unified_rom,_poc_flip_omega_unified_bank,_poc_step,_poc_pixels,_poc_width,_poc_height,_poc_frame_hz,_poc_key,_poc_key_mod,_poc_joy,_poc_joy_matrix,_poc_set_input_device,_poc_mouse_motion,_poc_mouse_button,_poc_mouse_clear,_poc_audio_reset,_poc_audio_avail,_poc_audio_read_pos,_poc_audio_buffer,_poc_audio_advance,_poc_load_disk,_poc_eject_disk,_poc_has_floppy,_poc_load_cassette,_poc_eject_cassette,_poc_disk_motor,_poc_dump_screen_text,_poc_set_sunrise,_poc_sunrise_enabled,_poc_sunrise_slot,_poc_mount_ide,_poc_eject_ide,_poc_ide_mounted,_poc_ide_writable,_poc_ide_activity,_poc_install_scsi_rom,_poc_scsi_rom_ready,_poc_set_scsi_target_id,_poc_scsi_target_id,_poc_set_scsi_io_base,_poc_scsi_io_base,_poc_set_scsi,_poc_scsi_enabled,_poc_scsi_slot,_poc_mount_scsi,_poc_eject_scsi_disk,_poc_scsi_disk_mounted,_poc_scsi_disk_writable,_poc_scsi_activity,_poc_set_sd_mapper,_poc_sd_mapper_enabled,_poc_sd_mapper_slot,_poc_mount_sd_card,_poc_eject_sd_card,_poc_sd_card_mounted,_poc_sd_activity_mask,_poc_set_powergraph_v9990,_poc_powergraph_v9990_enabled,_poc_powergraph_v9990_slot,_poc_set_powergraph_video_source,_poc_powergraph_video_source,_poc_powergraph_output_active,_poc_set_unapi,_poc_unapi_enabled,_poc_unapi_guest_active,_poc_unapi_activity,_poc_unapi_probe,_poc_unapi_dns_result,_poc_unapi_tcp_open_result,_poc_unapi_udp_open_result,_poc_autorun,_malloc,_free' \
	    -sEXPORTED_RUNTIME_METHODS='HEAPU8,HEAPU32,wasmMemory,getValue,setValue,ccall,cwrap,FS' \
	    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
	    --embed-file ../ROMS/cbios_main_msx1.rom@roms/cbios_main_msx1.rom \
	    --embed-file ../ROMS/cbios_logo_msx1.rom@roms/cbios_logo_msx1.rom \
	    --embed-file ../ROMS/rainbios_msx2.rom@roms/rainbios_msx2.rom \
	    --embed-file ../ROMS/rainbios_msx2_sub.rom@roms/rainbios_msx2_sub.rom \
	    --embed-file ../ROMS/rainbios_omega.rom@roms/rainbios_omega.rom \
	    --embed-file ../ROMS/rainbios_nms8250_disk.rom@roms/rainbios_nms8250_disk.rom \
	    --embed-file ../ROMS/Nextor-2.1.1.SunriseIDE.ROM@roms/nextor-sunrise.rom \
	    --embed-file ../ROMS/SDM\ V2\ Nextor2.1.1.rom@roms/sdmapper-v2-nextor.rom \
	    --embed-file ../ROMS/BertSCSI-v1-D0h-D7h.ROM@roms/bertscsi-v1-d0h-d7h.rom \
	    --embed-file ../ROMS/BertSCSI-v2-30h-37h.ROM@roms/bertscsi-v2-30h-37h.rom

$(BUILD_DIR)/%.o: ../src/%.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/poc_main.o: poc_main.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c poc_main.c -o $@

$(BUILD_DIR)/stubs.o: stubs.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c stubs.c -o $@

$(BUILD_DIR)/unapinet_wasm.o: unapinet_wasm.c unapinet_web.h | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c unapinet_wasm.c -o $@

$(BUILD_DIR)/unapinet_web.o: unapinet_web.c unapinet_web.h | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c unapinet_web.c -o $@

$(DIST_DIR)/index.html: index.html $(RUNTIME_ASSETS) | $(DIST_DIR)
	asset_revision=$$(sha256sum $(RUNTIME_ASSETS) | sha256sum | cut -c1-16); \
	    sed "s/@ASSET_REV@/$$asset_revision/g" $< > $@

$(DIST_DIR)/gamepad.js: gamepad.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/hardware-ui.js: hardware-ui.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/audio-scheduler.js: audio-scheduler.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/media-url.js: media-url.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/unapi-relay-protocol.js: unapi-relay-protocol.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/unapi-bridge.js: unapi-bridge.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/nextor-license.txt: ../ROMS/LICENSE-NEXTOR | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/%.css: %.css | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/app.js: app.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/brand-1983.png: $(BRAND_ICON) | $(DIST_DIR)
	cp $< $@

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

serve: all
	UNAPI_RELAY_HOST=$(UNAPI_HOST) UNAPI_RELAY_PORT=$(UNAPI_PORT) \
		npm --prefix relay start

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
	rm -f *.o *.ppm poc.js poc.wasm 1983.js 1983.wasm

.PHONY: all clean serve test check-wasm check-wasm-symbos check-wasm-scsi test-audio-scheduler test-gamepad \
	test-hardware-ui test-media-url test-theme test-ui-contract test-unapi \
	test-relay
