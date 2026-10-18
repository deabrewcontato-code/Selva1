/* Selva1 · the audio thread. The engine (selva1.wasm) runs here, 128 samples at a time; the panel talks to it
 * through messages:
 *   {t:"set", a:[id, value, id, value, ...]}   parameters and sequencer steps
 *   {t:"on", n, v} {t:"off", n} {t:"cc", c, v} {t:"bend", v} {t:"at", v} {t:"panic"} {t:"play", v}
 * and gets back, about 30 times a second, the engine's view: step, playing, meters, voices, LFOs, envelopes.
 * Copyright (C) 2026 DeAbrew. */
class Selva1Processor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const bytes = options.processorOptions.wasm;
    const mod = new WebAssembly.Module(bytes);
    this.x = new WebAssembly.Instance(mod, {}).exports;
    this.x.s1_init(sampleRate);
    this.pl = this.x.s1_buf_l();
    this.pr = this.x.s1_buf_r();
    this.pt = this.x.s1_buf_tmp();
    this.views();
    this.tick = 0;
    this.every = Math.max(1, Math.round(sampleRate / 128 / 30));
    this.port.onmessage = (e) => this.msg(e.data);
    this.port.postMessage({ t: "ready" });
  }
  views() {
    const buf = this.x.memory.buffer;
    this.buf = buf;
    this.L = new Float32Array(buf, this.pl, 1024);
    this.R = new Float32Array(buf, this.pr, 1024);
    this.T = new Float32Array(buf, this.pt, 16);
  }
  msg(m) {
    const x = this.x;
    switch (m.t) {
      case "set": { const a = m.a; for (let i = 0; i + 1 < a.length; i += 2) x.s1_set(a[i], a[i + 1]); break; }
      case "on": x.s1_note_on(m.n, m.v); break;
      case "off": x.s1_note_off(m.n); break;
      case "cc": x.s1_cc(m.c, m.v); break;
      case "bend": x.s1_bend(m.v); break;
      case "at": x.s1_aftertouch(m.v); break;
      case "panic": x.s1_panic(); break;
      case "play": x.s1_transport(m.v ? 1 : 0); break;
    }
  }
  process(inputs, outputs) {
    const out = outputs[0];
    if (!out || !out[0]) return true;
    const n = out[0].length;
    if (this.x.memory.buffer !== this.buf) this.views();
    this.x.s1_render_io(n);
    out[0].set(this.L.subarray(0, n));
    if (out[1]) out[1].set(this.R.subarray(0, n));
    if (++this.tick >= this.every) {
      this.tick = 0;
      this.x.s1_ui_state(this.pt);
      if (this.x.memory.buffer !== this.buf) this.views();
      this.port.postMessage({ t: "ui", s: Array.from(this.T.subarray(0, 14)) });
    }
    return true;
  }
}
registerProcessor("selva1", Selva1Processor);
