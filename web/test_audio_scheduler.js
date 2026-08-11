"use strict";

const assert = require("assert");
const { createFrameClock, createScheduler } = require("./audio-scheduler.js");

function fakeAudio(sampleCount) {
  const memory = new ArrayBuffer(44100 * 4 * Int16Array.BYTES_PER_ELEMENT);
  const ring = new Int16Array(memory);
  for (let i = 0; i < sampleCount; i++) ring[i] = i ? 8192 : -8192;
  const state = { available: sampleCount, read: 0, reset: 0, advanced: 0 };
  const emulator = {
    HEAPU8: new Uint8Array(memory),
    _poc_audio_avail: () => state.available,
    _poc_audio_read_pos: () => state.read,
    _poc_audio_advance: frames => {
      state.read += frames;
      state.available -= frames;
      state.advanced += frames;
    },
    _poc_audio_reset: () => {
      state.available = 0;
      state.read = 0;
      ++state.reset;
    },
  };
  const sources = [];
  const buffers = [];
  const context = {
    currentTime: 0,
    state: "running",
    destination: {},
    createBuffer: (_channels, frames) => {
      const data = new Float32Array(frames);
      const buffer = { length: frames, getChannelData: () => data, data };
      buffers.push(buffer);
      return buffer;
    },
    createBufferSource: () => {
      const source = {
        stopped: false,
        connect() {},
        disconnect() {},
        start(time) { this.startTime = time; },
        stop() { this.stopped = true; },
      };
      sources.push(source);
      return source;
    },
  };
  return { emulator, context, state, sources, buffers };
}

let fake = fakeAudio(735);
let scheduler = createScheduler({
  emulator: fake.emulator,
  context: fake.context,
  ringPtr: 0,
  ringSize: 44100 * 4,
});
let result = scheduler.schedule();
assert.deepStrictEqual(result, { buffers: 1, frames: 735 });
assert.strictEqual(fake.buffers[0].length, 735, "partial frame must not be padded with silence");
assert.strictEqual(fake.buffers[0].data[0], -0.25);
assert.strictEqual(fake.buffers[0].data[1], 0.25);
assert.strictEqual(fake.state.advanced, 735);

fake = fakeAudio(2048);
scheduler = createScheduler({
  emulator: fake.emulator,
  context: fake.context,
  ringPtr: 0,
  ringSize: 44100 * 4,
  startDelay: 0.01,
  targetLead: 0.1,
});
result = scheduler.schedule();
assert.deepStrictEqual(result, { buffers: 2, frames: 2048 });
assert.deepStrictEqual(fake.buffers.map(buffer => buffer.length), [1024, 1024]);
scheduler.reset();
assert.strictEqual(fake.state.reset, 1);
assert.ok(fake.sources.every(source => source.stopped), "reset must cancel queued audio");

const clock = createFrameClock(60);
let frames = clock.consume(0);
for (let time = 20; time <= 1000; time += 20) frames += clock.consume(time);
assert.strictEqual(frames, 60, "a 60 Hz machine must execute 60 frames per second");
assert.ok(clock.consume(5000) <= 5, "background-tab recovery must be bounded");

console.log("web audio scheduler tests passed");
