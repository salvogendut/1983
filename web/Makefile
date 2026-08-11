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
CFLAGS    = -O2 -std=c11 -Wall -Wextra -I../src -Icompat -I.

SRCS = \
	../src/z80.c \
	../src/vdp.c \
	../src/psg.c \
	../src/msx.c \
	../src/cartridge.c \
	../src/cassette.c \
	../src/ata.c \
	../src/floppy.c \
	../src/wd2793.c \
	../src/megaflash.c \
	../src/rtc.c \
	../src/scc.c \
	../src/sdcard.c \
	../src/sd_mapper.c \
	../src/sunrise.c \
	../src/kbd.c \
	../src/paste.c

CORE_OBJS = $(patsubst ../src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJS = $(CORE_OBJS) $(BUILD_DIR)/poc_main.o $(BUILD_DIR)/stubs.o
WASM_ASSETS = $(DIST_DIR)/1983.js $(DIST_DIR)/1983.wasm
BRAND_ICON = ../icons/512x512/apps/io.github.salvogendut.Emulator1983.png
STYLE_ASSETS = styles.css theme-sonyhb-f1xd.css theme-retro-crt.css \
	theme-sapporo.css theme-sapporo-dark.css

all: $(DIST_DIR)/index.html $(addprefix $(DIST_DIR)/,$(STYLE_ASSETS)) $(DIST_DIR)/app.js \
	$(DIST_DIR)/gamepad.js $(DIST_DIR)/media-url.js \
	$(DIST_DIR)/brand-1983.png $(WASM_ASSETS)

test: test-gamepad test-media-url

test-gamepad:
	node test_gamepad.js

test-media-url:
	node test_media_url.js

$(WASM_ASSETS) &: $(OBJS) | $(DIST_DIR)
	$(EMCC) $(CFLAGS) $(OBJS) -o $(DIST_DIR)/1983.js \
	    -sMODULARIZE -sEXPORT_NAME=create1983 \
	    -sEXPORTED_FUNCTIONS='_poc_init,_poc_init_model,_poc_load_cartridge,_poc_reset,_poc_step,_poc_pixels,_poc_width,_poc_height,_poc_key,_poc_key_mod,_poc_joy,_poc_joy_matrix,_poc_audio_reset,_poc_audio_avail,_poc_audio_read_pos,_poc_audio_buffer,_poc_audio_advance,_poc_load_disk,_poc_eject_disk,_poc_load_cassette,_poc_eject_cassette,_poc_disk_motor,_poc_autorun,_malloc,_free' \
	    -sEXPORTED_RUNTIME_METHODS='HEAPU8,HEAPU32,wasmMemory,getValue,setValue,ccall,cwrap,FS' \
	    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
	    --embed-file ../ROMS/cbios_main_msx1.rom@roms/cbios_main_msx1.rom \
	    --embed-file ../ROMS/cbios_logo_msx1.rom@roms/cbios_logo_msx1.rom

$(BUILD_DIR)/%.o: ../src/%.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/poc_main.o: poc_main.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c poc_main.c -o $@

$(BUILD_DIR)/stubs.o: stubs.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c stubs.c -o $@

$(DIST_DIR)/index.html: index.html | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/gamepad.js: gamepad.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/media-url.js: media-url.js | $(DIST_DIR)
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
	python3 -m http.server 8080 --directory $(DIST_DIR)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
	rm -f *.o *.ppm poc.js poc.wasm 1983.js 1983.wasm

.PHONY: all clean serve test test-gamepad test-media-url
