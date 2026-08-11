"use strict";

// KeyboardEvent.code to SDL_Scancode (SDL_scancode.h).
const CODE2SCAN = {
  KeyA:4, KeyB:5, KeyC:6, KeyD:7, KeyE:8, KeyF:9, KeyG:10, KeyH:11, KeyI:12,
  KeyJ:13, KeyK:14, KeyL:15, KeyM:16, KeyN:17, KeyO:18, KeyP:19, KeyQ:20,
  KeyR:21, KeyS:22, KeyT:23, KeyU:24, KeyV:25, KeyW:26, KeyX:27, KeyY:28, KeyZ:29,
  Digit1:30, Digit2:31, Digit3:32, Digit4:33, Digit5:34, Digit6:35, Digit7:36,
  Digit8:37, Digit9:38, Digit0:39,
  Enter:40, Escape:41, Backspace:42, Tab:43, Space:44,
  Minus:45, Equal:46, BracketLeft:47, BracketRight:48, Backslash:49,
  Semicolon:51, Quote:52, Backquote:53, Comma:54, Period:55, Slash:56,
  CapsLock:57, Insert:73, Home:74, Delete:76,
  ArrowRight:79, ArrowLeft:80, ArrowDown:81, ArrowUp:82,
  F1:58, F2:59, F3:60, F4:61, F5:62, F6:63, F7:64, F8:65, F9:66,
  NumpadEnter:88, NumpadAdd:87, NumpadSubtract:86, NumpadMultiply:85,
  NumpadDivide:84, NumpadDecimal:99,
  ControlLeft:224, ShiftLeft:225, AltLeft:226,
  ControlRight:228, ShiftRight:229, AltRight:230,
};

const SDL_KMOD_SHIFT = 0x0002;

const $ = id => document.getElementById(id);
const canvas = $("screen");
const screenFrame = $("screenFrame");
const statusEl = $("status");
const toastEl = $("toast");
const ledPowerEl = $("ledPower");
const ledAEl = $("ledA");
const ledInputEl = $("ledInput");
const ledAudioEl = $("ledAudio");
const ctx = canvas.getContext("2d");
const VW = 768;
const VH = 576;

let framebufferPtr = 0;
let frameW = 0;
let frameH = 0;
let pixelSharp = true;
let monochromeGreen = false;
let toastTimer = 0;
let inputLedTimer = 0;

function setStatus(message) {
  statusEl.textContent = message;
}

function showToast(message) {
  toastEl.textContent = message;
  toastEl.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toastEl.classList.remove("show"), 3200);
}

const THEMES = {
  "sonyhb-f1xd": "SONYHB-F1XD",
  "retro-crt": "Retro CRT",
  "sapporo": "Sapporo",
  "sapporo-dark": "Sapporo Dark",
};
const THEME_STORAGE_KEY = "javascript1983.theme";
const themePickerEl = document.querySelector(".theme-picker");
const themeButtonEl = $("themeButton");
const themeMenuEl = $("themeMenu");
const themeNameEl = $("themeName");

function setThemeMenu(open) {
  themeMenuEl.hidden = !open;
  themeButtonEl.setAttribute("aria-expanded", String(open));
}

function resolveTheme(theme) {
  if (!theme) return "sonyhb-f1xd";
  const requested = String(theme).toLowerCase();
  return Object.keys(THEMES).find(key =>
    key.toLowerCase() === requested || THEMES[key].toLowerCase() === requested
  ) || "sonyhb-f1xd";
}

function applyTheme(theme, persist = true) {
  const selected = resolveTheme(theme);
  document.documentElement.dataset.theme = selected;
  themeNameEl.textContent = THEMES[selected];
  for (const option of themeMenuEl.querySelectorAll("[data-theme]"))
    option.setAttribute("aria-checked", String(option.dataset.theme === selected));
  if (persist) {
    try {
      localStorage.setItem(THEME_STORAGE_KEY, selected);
    } catch (_) {
      // Storage can be unavailable in privacy-restricted browser contexts.
    }
  }
}

let savedTheme = "sonyhb-f1xd";
try {
  savedTheme = localStorage.getItem(THEME_STORAGE_KEY) || "sonyhb-f1xd";
} catch (_) {
  // Keep the default theme when storage access is unavailable.
}
const requestedTheme = new URLSearchParams(window.location.search).get("theme");
applyTheme(requestedTheme || savedTheme, false);

themeButtonEl.addEventListener("click", event => {
  event.stopPropagation();
  setThemeMenu(themeMenuEl.hidden);
});
themeMenuEl.addEventListener("click", event => {
  const option = event.target.closest("[data-theme]");
  if (!option) return;
  applyTheme(option.dataset.theme);
  setThemeMenu(false);
  themeButtonEl.focus();
  showToast(THEMES[option.dataset.theme] + " theme selected");
});
themePickerEl.addEventListener("keydown", event => {
  if (event.key === "Escape") {
    setThemeMenu(false);
    themeButtonEl.focus();
  }
});
document.addEventListener("click", event => {
  if (!themePickerEl.contains(event.target)) setThemeMenu(false);
});

const msxKeyboardEl = document.querySelector(".sony-msx-keyboard");
const msxKeyboardKeysEl = $("msxKeyboardKeys");
const msxKeyboardToggleEl = $("msxKeyboardToggle");

function setMsxKeyboardOpen(open) {
  msxKeyboardEl.dataset.keyboardOpen = String(open);
  msxKeyboardKeysEl.hidden = !open;
  msxKeyboardToggleEl.setAttribute("aria-expanded", String(open));
  msxKeyboardToggleEl.textContent = open ? "Hide keyboard" : "Show keyboard";
}

msxKeyboardToggleEl.addEventListener("click", () => {
  setMsxKeyboardOpen(msxKeyboardKeysEl.hidden);
});
setMsxKeyboardOpen(false);

function pulseInputLed() {
  ledInputEl.classList.add("on");
  clearTimeout(inputLedTimer);
  inputLedTimer = setTimeout(() => ledInputEl.classList.remove("on"), 120);
}

function setScreenScale(value) {
  const scale = Number(value);
  document.documentElement.style.setProperty("--screen-scale", String(scale / 100));
  $("screenScale").value = String(scale);
  $("scaleValue").textContent = scale + "%";
  const rotation = -115 + ((scale - 70) / 30) * 230;
  $("sizeNeedle").style.transform = "rotate(" + rotation + "deg)";
}

function updatePixelMode() {
  pixelSharp = $("pixelToggle").checked;
  canvas.style.imageRendering = pixelSharp ? "pixelated" : "auto";
  updateScreenModeReadout();
}

function updateScreenModeReadout() {
  $("screenMode").textContent = frameW + " x " + frameH + " / " +
    (pixelSharp ? "Sharp" : "Smooth") + " / " +
    (monochromeGreen ? "Green" : "Color");
}

const DISPLAY_MODE_STORAGE_KEY = "javascript1983.displayMode";
const colorModeEl = $("colorMode");
function setDisplayColorMode(green, persist = true) {
  monochromeGreen = green;
  colorModeEl.setAttribute("aria-pressed", String(green));
  colorModeEl.classList.toggle("active", green);
  $("colorModeName").textContent = green ? "Green monochrome" : "Color display";
  colorModeEl.querySelector("small").textContent = green
    ? "Switch to full color"
    : "Switch to green monochrome";
  updateScreenModeReadout();
  if (persist) {
    try {
      localStorage.setItem(DISPLAY_MODE_STORAGE_KEY, green ? "green" : "color");
    } catch (_) {
      // Keep the in-memory selection when storage is unavailable.
    }
  }
}

$("screenScale").addEventListener("input", event => setScreenScale(event.target.value));
$("fitScreen").addEventListener("click", () => {
  setScreenScale(100);
  showToast("Display fitted to the receiver");
});
$("pixelToggle").addEventListener("change", updatePixelMode);
colorModeEl.addEventListener("click", () => {
  setDisplayColorMode(!monochromeGreen);
  showToast(monochromeGreen ? "Green monochrome display enabled" : "Color display restored");
});
$("fullscreen").addEventListener("click", async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await screenFrame.requestFullscreen();
  } catch (error) {
    setStatus("Fullscreen unavailable: " + error.message);
  }
});
$("expansion").addEventListener("click", () => {
  showToast("Expansion bay reserved for future browser devices");
});
setScreenScale(100);
updatePixelMode();
let savedDisplayMode = "color";
try {
  savedDisplayMode = localStorage.getItem(DISPLAY_MODE_STORAGE_KEY) || "color";
} catch (_) {
  // Keep the color display when storage access is unavailable.
}
setDisplayColorMode(savedDisplayMode === "green", false);

create1983().then(m => {
  if (m._poc_init() !== 0) {
    setStatus("Emulator initialization failed");
    return;
  }

  ledPowerEl.classList.add("on");
  setStatus("MSX1 booting - click the display for keyboard focus");

  framebufferPtr = m._poc_pixels();
  frameW = m._poc_width();
  frameH = m._poc_height();
  const modelEl = $("model");
  const resetEl = $("reset");
  const diskfileEl = $("diskfile");
  const disknameEl = $("diskname");
  const diskEjectEl = $("diskEject");
  const cartfileEl = $("cartfile");
  const cartnameEl = $("cartname");
  const cartEjectEl = $("cartEject");
  const cassfileEl = $("cassfile");
  const cassnameEl = $("cassname");
  const cassEjectEl = $("cassEject");
  const joytoggleEl = $("joytoggle");
  const joystatusEl = $("joystatus");
  const joymatrixEl = $("joymatrix");

  let currentModel = 0;
  let audioCtx = null;
  let audioState = null;
  let nextAudioStart = 0;
  let prevGamepad = null;
  let joyEnabled = true;
  let ledState = 0;
  const heldKeys = new Map();
  const virtualKeys = new Set();
  const latchedVirtualModifiers = new Set();

  function isGuestFunctionScancode(scancode) {
    return (scancode >= 58 && scancode <= 62) || scancode === 64 || scancode === 65;
  }

  function sendMsxKey(scancode, pressed) {
    const mod = isGuestFunctionScancode(scancode) ? SDL_KMOD_SHIFT : 0;
    m._poc_key_mod(scancode, pressed ? 1 : 0, mod);
  }

  function pressVirtualKey(scancode) {
    if (virtualKeys.has(scancode)) return;
    const alreadyPressed = heldKeys.has(scancode);
    virtualKeys.add(scancode);
    if (!alreadyPressed) sendMsxKey(scancode, true);
  }

  function releaseVirtualKey(scancode) {
    if (!virtualKeys.delete(scancode)) return;
    if (!heldKeys.has(scancode)) sendMsxKey(scancode, false);
  }

  function setModifierUi(scancode, active) {
    for (const button of msxKeyboardKeysEl.querySelectorAll(
      `[data-modifier][data-scancode="${scancode}"]`
    )) {
      button.classList.toggle("latched", active);
      button.setAttribute("aria-pressed", String(active));
    }
  }

  function releaseLatchedModifiers() {
    for (const scancode of latchedVirtualModifiers) {
      releaseVirtualKey(scancode);
      setModifierUi(scancode, false);
    }
    latchedVirtualModifiers.clear();
  }

  function toggleVirtualModifier(scancode) {
    if (latchedVirtualModifiers.delete(scancode)) {
      releaseVirtualKey(scancode);
      setModifierUi(scancode, false);
    } else {
      latchedVirtualModifiers.add(scancode);
      pressVirtualKey(scancode);
      setModifierUi(scancode, true);
    }
  }

  function releaseAllVirtualKeys() {
    for (const scancode of [...virtualKeys]) releaseVirtualKey(scancode);
    latchedVirtualModifiers.clear();
    for (const button of msxKeyboardKeysEl.querySelectorAll("[data-scancode]")) {
      button.classList.remove("active", "latched");
      if (button.hasAttribute("data-modifier"))
        button.setAttribute("aria-pressed", "false");
    }
  }

  function virtualKeyButton(target) {
    return target.closest("button[data-scancode]");
  }

  msxKeyboardKeysEl.addEventListener("pointerdown", event => {
    const button = virtualKeyButton(event.target);
    if (!button) return;
    event.preventDefault();
    startAudio();
    const scancode = Number(button.dataset.scancode);
    if (button.hasAttribute("data-modifier")) {
      toggleVirtualModifier(scancode);
    } else {
      pressVirtualKey(scancode);
      button.classList.add("active");
      button.setPointerCapture(event.pointerId);
    }
    pulseInputLed();
  });

  function finishVirtualPointer(event) {
    const button = virtualKeyButton(event.target);
    if (!button || button.hasAttribute("data-modifier")) return;
    releaseVirtualKey(Number(button.dataset.scancode));
    button.classList.remove("active");
    releaseLatchedModifiers();
  }

  msxKeyboardKeysEl.addEventListener("pointerup", finishVirtualPointer);
  msxKeyboardKeysEl.addEventListener("pointercancel", finishVirtualPointer);
  msxKeyboardKeysEl.addEventListener("lostpointercapture", finishVirtualPointer);
  msxKeyboardKeysEl.addEventListener("click", event => {
    if (event.detail !== 0) return;
    const button = virtualKeyButton(event.target);
    if (!button) return;
    startAudio();
    const scancode = Number(button.dataset.scancode);
    if (button.hasAttribute("data-modifier")) {
      toggleVirtualModifier(scancode);
    } else {
      pressVirtualKey(scancode);
      button.classList.add("active");
      setTimeout(() => {
        releaseVirtualKey(scancode);
        button.classList.remove("active");
        releaseLatchedModifiers();
      }, 90);
    }
    pulseInputLed();
  });
  msxKeyboardToggleEl.addEventListener("click", () => {
    if (msxKeyboardKeysEl.hidden) releaseAllVirtualKeys();
  });
  window.addEventListener("blur", releaseAllVirtualKeys);

  function clearDiskUi() {
    disknameEl.textContent = "No disk loaded";
    diskEjectEl.disabled = true;
    diskfileEl.value = "";
  }

  function clearCartUi() {
    cartnameEl.textContent = "No cartridge loaded";
    cartEjectEl.disabled = true;
    cartfileEl.value = "";
  }

  function clearCassUi() {
    cassnameEl.textContent = "No tape loaded";
    cassEjectEl.disabled = true;
    cassfileEl.value = "";
  }

  function releaseAllJoy() {
    for (let column = 0; column < 8; column++)
      m._poc_joy(column, 0);
    prevGamepad = null;
  }

  function reinit(model, cartridge) {
    const rc = cartridge !== undefined
      ? m.ccall("poc_load_cartridge", "number", ["string"], [cartridge])
      : m._poc_init_model(model, 0);
    if (rc !== 0) {
      setStatus("Machine initialization failed (MSX2 needs a real MSX2.ROM)");
      return false;
    }
    currentModel = model;
    modelEl.value = String(model);
    framebufferPtr = m._poc_pixels();
    frameW = m._poc_width();
    frameH = m._poc_height();
    m._poc_audio_reset();
    if (audioCtx) nextAudioStart = audioCtx.currentTime + 0.3;
    releaseAllJoy();
    releaseAllVirtualKeys();
    clearDiskUi();
    if (cartridge === undefined) clearCartUi();
    clearCassUi();
    updateScreenModeReadout();
    setStatus("Machine reset");
    return true;
  }

  modelEl.addEventListener("change", () => {
    const model = Number(modelEl.value);
    if (reinit(model)) {
      clearCartUi();
      showToast(model === 1 ? "MSX2 selected" : "MSX1 selected");
    } else {
      modelEl.value = String(currentModel);
    }
  });

  resetEl.addEventListener("click", () => {
    m._poc_reset();
    m._poc_audio_reset();
    if (audioCtx) nextAudioStart = audioCtx.currentTime + 0.3;
    releaseAllJoy();
    releaseAllVirtualKeys();
    setStatus("Warm reset complete");
    showToast("MSX reset");
    canvas.focus();
  });

  function mountCartridge(data, name, path) {
    m.FS.writeFile(path, data);
    const rc = m.ccall("poc_load_cartridge", "number", ["string"], [path]);
    if (rc !== 0) throw new Error("unsupported or damaged cartridge ROM");
    cartnameEl.textContent = name;
    cartEjectEl.disabled = false;
    setStatus("Cartridge: " + name);
    showToast("Cartridge loaded");
  }

  function mountDisk(data, name, path) {
    m.FS.writeFile(path, data);
    const rc = m.ccall("poc_load_disk", "number", ["string"], [path]);
    if (rc !== 0) throw new Error("unsupported or damaged disk image");
    disknameEl.textContent = name;
    diskEjectEl.disabled = false;
    setStatus("Drive A: " + name);
    showToast("Disk loaded into Drive A");
  }

  function mountCassette(data, name, path) {
    m.FS.writeFile(path, data);
    const rc = m.ccall("poc_load_cassette", "number", ["string"], [path]);
    if (rc !== 0) throw new Error("unsupported cassette image");
    cassnameEl.textContent = name;
    cassEjectEl.disabled = false;
    setStatus("Cassette: " + name);
    showToast("Cassette loaded");
  }

  async function loadCartridgeFile(file) {
    if (!file) return;
    try {
      const data = new Uint8Array(await file.arrayBuffer());
      mountCartridge(data, file.name, "/uploaded.rom");
    } catch (error) {
      setStatus("Cartridge load failed: " + error.message);
      showToast("Could not load " + file.name);
    }
  }

  async function loadDiskFile(file) {
    if (!file) return;
    try {
      const data = new Uint8Array(await file.arrayBuffer());
      mountDisk(data, file.name, "/uploaded.dsk");
    } catch (error) {
      setStatus("Disk load failed: " + error.message);
      showToast("Could not load " + file.name);
    }
  }

  async function loadCassetteFile(file) {
    if (!file) return;
    try {
      const data = new Uint8Array(await file.arrayBuffer());
      mountCassette(data, file.name, "/uploaded.cas");
    } catch (error) {
      setStatus("Cassette load failed: " + error.message);
      showToast("Could not load " + file.name);
    }
  }

  cartfileEl.addEventListener("change", () => loadCartridgeFile(cartfileEl.files[0]));
  cartEjectEl.addEventListener("click", () => {
    m._poc_reset();
    m._poc_audio_reset();
    releaseAllJoy();
    clearCartUi();
    setStatus("Cartridge ejected");
  });
  diskfileEl.addEventListener("change", () => loadDiskFile(diskfileEl.files[0]));
  diskEjectEl.addEventListener("click", () => {
    m._poc_eject_disk();
    clearDiskUi();
    setStatus("Drive A ejected");
  });
  cassfileEl.addEventListener("change", () => loadCassetteFile(cassfileEl.files[0]));
  cassEjectEl.addEventListener("click", () => {
    m._poc_eject_cassette();
    clearCassUi();
    setStatus("Cassette ejected");
  });

  async function fetchServerMedia(url, kind) {
    const name = JS1983Media.filenameFromUrl(url, kind);
    setStatus("Fetching " + kind + ": " + name);
    const response = await fetch(url);
    if (!response.ok)
      throw new Error(kind + " request returned HTTP " + response.status);
    const data = new Uint8Array(await response.arrayBuffer());
    if (!data.byteLength) throw new Error(kind + " response was empty");
    return { data, name };
  }

  async function bootstrapServerMedia() {
    let media;
    try {
      media = JS1983Media.parseStartupMedia(
        window.location.search,
        document.baseURI
      );
    } catch (error) {
      setStatus("Media URL error: " + error.message);
      showToast("Invalid server media URL");
      return;
    }
    if (!media.disk && !media.cartridge) return;

    try {
      if (media.cartridge) {
        const cartridge = await fetchServerMedia(media.cartridge, "cartridge");
        mountCartridge(cartridge.data, cartridge.name, "/server-cartridge.rom");
      }
      if (media.disk) {
        const disk = await fetchServerMedia(media.disk, "disk");
        mountDisk(disk.data, disk.name, "/server-disk.dsk");
        if (media.autorun) {
          m._poc_reset();
          m._poc_audio_reset();
          if (audioCtx) nextAudioStart = audioCtx.currentTime + 0.3;
          releaseAllJoy();
          const rc = m.ccall(
            "poc_autorun",
            "number",
            ["string", "number"],
            [media.autorun, 42]
          );
          if (rc !== 0) throw new Error("invalid autorun filename");
          setStatus(
            "Drive A: " + disk.name + " - autorun " + media.autorun + " armed"
          );
          showToast("Autorun " + media.autorun + " armed");
        }
      }
    } catch (error) {
      setStatus("Server media failed: " + error.message);
      showToast("Could not load server media");
    }
  }

  // The emulator fills a mono ring at 60 Hz. Schedule short buffers ahead
  // of the Web Audio clock so canvas work cannot starve playback.
  const AUDIO_CHUNK = 2048;
  function startAudio() {
    if (audioCtx) {
      audioCtx.resume();
      return;
    }
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    m._poc_audio_reset();
    audioState = { ringPtr: m._poc_audio_buffer(), ringSize: 44100 * 4 };
    nextAudioStart = audioCtx.currentTime + 0.3;
    audioCtx.resume().then(() => ledAudioEl.classList.add("on"));
  }

  function scheduleAudio() {
    if (!audioCtx || audioCtx.state !== "running") return;
    if (nextAudioStart < audioCtx.currentTime + 0.05)
      nextAudioStart = audioCtx.currentTime + 0.05;
    while (nextAudioStart - audioCtx.currentTime < 0.25) {
      const available = m._poc_audio_avail();
      const frames = Math.min(AUDIO_CHUNK, available);
      if (frames === 0) break;
      const readPosition = m._poc_audio_read_pos();
      const samples = new Int16Array(
        m.HEAPU8.buffer,
        audioState.ringPtr,
        audioState.ringSize
      );
      const buffer = audioCtx.createBuffer(1, AUDIO_CHUNK, 44100);
      const mono = buffer.getChannelData(0);
      for (let i = 0; i < frames; i++)
        mono[i] = samples[(readPosition + i) % audioState.ringSize] / 32768;
      m._poc_audio_advance(frames);
      const source = audioCtx.createBufferSource();
      source.buffer = buffer;
      source.connect(audioCtx.destination);
      source.start(nextAudioStart);
      nextAudioStart += AUDIO_CHUNK / 44100;
    }
  }

  window.addEventListener("pointerdown", startAudio, { once: true });

  function setJoystickEnabled(enabled) {
    joyEnabled = enabled;
    joytoggleEl.checked = enabled;
    if (!enabled) releaseAllJoy();
    joystatusEl.textContent = enabled ? "Joystick: enabled" : "Joystick: disabled";
  }

  joytoggleEl.addEventListener("change", () => setJoystickEnabled(joytoggleEl.checked));

  function gamepadUnavailableReason() {
    if (!window.isSecureContext)
      return "Gamepad API requires HTTPS or localhost";
    if (typeof navigator.getGamepads !== "function")
      return "Gamepad API is unavailable in this browser";
    const policy = document.permissionsPolicy || document.featurePolicy;
    if (policy && typeof policy.allowsFeature === "function" && !policy.allowsFeature("gamepad"))
      return "Gamepad API is blocked by Permissions Policy";
    return "";
  }

  function updateMsxJoyStatus() {
    const row = m._poc_joy_matrix() & 0xff;
    const names = ["UP", "DOWN", "LEFT", "RIGHT", "-", "-", "A", "B"];
    const active = names.filter((_, column) => !(row & (1 << column)));
    joymatrixEl.textContent = "MSX joystick: " +
      (active.length ? active.join(" ") : "idle") +
      " (row 7 = 0x" + row.toString(16).padStart(2, "0").toUpperCase() + ")";
  }

  function pollGamepad() {
    const unavailable = gamepadUnavailableReason();
    if (unavailable) {
      joystatusEl.textContent = "Joystick unavailable: " + unavailable;
      return;
    }
    let pads;
    try {
      pads = navigator.getGamepads();
    } catch (error) {
      joystatusEl.textContent = "Joystick unavailable: " + error.message;
      return;
    }
    let gamepad = null;
    for (const pad of pads) {
      if (pad && pad.connected) {
        gamepad = pad;
        break;
      }
    }
    if (!gamepad) {
      if (prevGamepad) releaseAllJoy();
      joystatusEl.textContent = "Joystick: no controller exposed";
      updateMsxJoyStatus();
      return;
    }

    const mapped = JS1983Gamepad.mapGamepad(gamepad);
    const state = mapped.state;
    const names = ["UP", "DOWN", "LEFT", "RIGHT", "A", "B"];
    if (!joyEnabled) {
      if (prevGamepad) releaseAllJoy();
      return;
    }

    if (state.some(Boolean)) {
      joystatusEl.textContent = "Joystick [" + mapped.profile + "]: " +
        names.filter((_, column) => state[column]).join(" ");
      pulseInputLed();
    } else {
      joystatusEl.textContent = "Joystick: " + gamepad.id;
    }

    if (prevGamepad) {
      for (let column = 0; column < 6; column++) {
        if (prevGamepad[column] !== state[column]) m._poc_joy(column, state[column]);
      }
    } else {
      for (let column = 0; column < 6; column++) {
        if (state[column]) m._poc_joy(column, 1);
      }
    }
    prevGamepad = state;
    updateMsxJoyStatus();
  }

  window.addEventListener("gamepadconnected", event => {
    joystatusEl.textContent = "Joystick: connected " + event.gamepad.id;
    showToast("Game controller connected");
  });
  window.addEventListener("gamepaddisconnected", () => {
    releaseAllJoy();
    joystatusEl.textContent = "Joystick: disconnected";
  });
  window.addEventListener("focus", pollGamepad);
  $("joydetect").addEventListener("click", () => {
    startAudio();
    pollGamepad();
    showToast("Scanning browser game controllers");
  });
  setInterval(pollGamepad, 100);

  canvas.addEventListener("click", () => {
    canvas.focus();
    startAudio();
  });
  canvas.addEventListener("contextmenu", event => event.preventDefault());

  window.addEventListener("keydown", event => {
    const scancode = CODE2SCAN[event.code];
    if (scancode === undefined || document.activeElement !== canvas) return;
    event.preventDefault();
    startAudio();
    // Guest F1..F5, SELECT and STOP are the documented Shift+Fn chords.
    const guestFunction = isGuestFunctionScancode(scancode);
    const mod = guestFunction && event.shiftKey ? SDL_KMOD_SHIFT : 0;
    if (!heldKeys.has(scancode)) {
      const alreadyPressed = virtualKeys.has(scancode);
      heldKeys.set(scancode, mod);
      if (!alreadyPressed) m._poc_key_mod(scancode, 1, mod);
      pulseInputLed();
    }
  });
  window.addEventListener("keyup", event => {
    const scancode = CODE2SCAN[event.code];
    if (scancode === undefined || !heldKeys.has(scancode)) return;
    event.preventDefault();
    const mod = heldKeys.get(scancode);
    heldKeys.delete(scancode);
    if (!virtualKeys.has(scancode)) m._poc_key_mod(scancode, 0, mod);
  });
  canvas.addEventListener("blur", () => {
    for (const [scancode, mod] of heldKeys) {
      if (!virtualKeys.has(scancode)) m._poc_key_mod(scancode, 0, mod);
    }
    heldKeys.clear();
  });

  for (const eventName of ["dragenter", "dragover"]) {
    screenFrame.addEventListener(eventName, event => {
      event.preventDefault();
      screenFrame.classList.add("dragging");
    });
  }
  for (const eventName of ["dragleave", "drop"]) {
    screenFrame.addEventListener(eventName, event => {
      event.preventDefault();
      screenFrame.classList.remove("dragging");
    });
  }
  screenFrame.addEventListener("drop", event => {
    const file = event.dataTransfer.files[0];
    if (!file) return;
    const lowerName = file.name.toLowerCase();
    if (lowerName.endsWith(".dsk")) loadDiskFile(file);
    else if (lowerName.endsWith(".cas")) loadCassetteFile(file);
    else if (lowerName.endsWith(".rom") || lowerName.endsWith(".mx1") ||
             lowerName.endsWith(".mx2")) loadCartridgeFile(file);
    else showToast("Use a ROM, DSK or CAS image");
  });

  function updateLed() {
    const on = m._poc_disk_motor();
    if (on !== ledState) {
      ledState = on;
      ledAEl.classList.toggle("on", Boolean(on));
    }
  }

  // The VDP framebuffer is at native resolution (256x192 MSX1, 512x212 MSX2);
  // render it into an offscreen canvas and stretch to the 768x576 screen.
  const offscreen = document.createElement("canvas");
  const offctx = offscreen.getContext("2d");
  let image = null;

  function ensureOffscreen(w, h) {
    if (offscreen.width === w && offscreen.height === h) return;
    offscreen.width = w;
    offscreen.height = h;
    image = offctx.createImageData(w, h);
  }

  let lastFrame = 0;
  function frame(time) {
    while (time - lastFrame >= 20) {
      m._poc_step();
      lastFrame += 20;
      scheduleAudio();
      pollGamepad();
      updateLed();
    }

    const w = m._poc_width();
    const h = m._poc_height();
    if (w !== frameW || h !== frameH) {
      frameW = w;
      frameH = h;
      updateScreenModeReadout();
    }
    ensureOffscreen(frameW, frameH);
    const pixels = m.HEAPU32.subarray(framebufferPtr >> 2, (framebufferPtr >> 2) + frameW * frameH);
    for (let i = 0, destination = 0; i < frameW * frameH; i++, destination += 4) {
      const color = pixels[i];
      const red = (color >> 16) & 0xff;
      const green = (color >> 8) & 0xff;
      const blue = color & 0xff;
      if (monochromeGreen) {
        // Rec. 709 integer luminance mapped onto a green phosphor response.
        const luminance = (red * 54 + green * 183 + blue * 19) >> 8;
        image.data[destination] = (luminance * 7) >> 5;
        image.data[destination + 1] = Math.min(255, (luminance * 5) >> 2);
        image.data[destination + 2] = (luminance * 11) >> 5;
      } else {
        image.data[destination] = red;
        image.data[destination + 1] = green;
        image.data[destination + 2] = blue;
      }
      image.data[destination + 3] = 0xff;
    }
    offctx.putImageData(image, 0, 0);
    ctx.imageSmoothingEnabled = !pixelSharp;
    ctx.drawImage(offscreen, 0, 0, frameW, frameH, 0, 0, VW, VH);
    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
  bootstrapServerMedia();
}).catch(error => {
  setStatus("Failed to start: " + error);
});
