# 1983 - Controls

This is the reference for 1983's keyboard and input controls. Machine setup,
media, and extensions are covered in [`USAGE.md`](USAGE.md).

## Keys

| Key | Action |
|-----|--------|
| F4 | Save a PPM screenshot (with a camera-shutter sound) |
| F5 | Reset |
| F6 | Toggle animated GIF recording |
| F8 | Monitor/disassembler placeholder |
| F9 | Open / save-and-close the options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Pause | Pause or resume |
| Ctrl++ / Ctrl+- | Adjust window scale |
| Ctrl+V | Paste host clipboard into the MSX |
| Shift+F1…F5 | MSX F1…F5 |
| Shift+F7 / Shift+F8 | MSX SELECT / STOP |
| Ctrl+Enter | Release captured mouse |

SDL scancodes map positionally to the international MSX keyboard: Left
Ctrl=CTRL, Left Alt=GRAPH, Right Alt=CODE, Right Ctrl=ACC/dead key; both
Shift keys, editing keys, arrows, and the numeric keypad are supported.

## Options overlay

Left/Right change section, Up/Down select, Enter activates, F9 saves, Escape
closes (or offers to discard). In **Extensions**, Enter toggles a device,
Space edits its settings, Delete clears saved settings. **General > Extra
Hardware** reveals Extensions; **General > Tinker** reveals Advanced.

## GIF capture

**F6** or `--gif-out PATH` records an animated GIF. The Advanced section
cycles resolution (720/540/360/240/180), frame rate (25/20/10/5), and encoder
(built-in GIF89a or FFmpeg optimize).

## Mouse and gamepad

With the selected port set to Mouse, click the emulator window to capture
relative movement; the left/right host buttons map to MSX A/B. Ctrl+Enter,
F9, reset, or losing focus releases capture. With the selected Main Input
connector set to Joystick, the primary SDL3 gamepad drives it while connected.

## Clipboard paste

Ctrl+V replays the host clipboard into the emulated keyboard matrix one key at
a time, verbatim and without an extra Return.