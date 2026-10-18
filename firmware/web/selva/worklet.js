// SPDX-License-Identifier: GPL-3.0-only
// Selva1 web panel: the audio thread runs the firmware's DSP (WebAssembly). Messages from the page: a batch of
// [kind, a, b] (p: track parameter a = b, x: Selva1 value a = b, g: global a = b, n: note a velocity b (0 = off),
// r: factory preset a, k: panic). Part 1 (track 0) only.
class SelvaProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const module = new WebAssembly.Module(options.processorOptions.bytes);
    this.w = new WebAssembly.Instance(module, {}).exports;
    if (this.w._initialize) this.w._initialize();
    this.w.preview_init();
    this.w.preview_select(0, 0);
    this.frames = this.w.preview_frames();
    this.ptr = this.w.preview_render();
    this.pos = this.frames;
    this.peak = 0;
    this.count = 0;
    this.port.onmessage = ({ data }) => {
      try {
        for (const [kind, a, b] of data) this.apply(kind, a, b);
      } catch (error) {
        this.port.postMessage({ error: String(error && error.message || error) });
      }
    };
  }
  apply(kind, a, b) {
    const w = this.w;
    if (kind === 'p') w.preview_param(0, a, b);
    else if (kind === 'x') w.preview_selva(0, a, b);
    else if (kind === 'g') w.preview_global(a, b);
    else if (kind === 'n') w.preview_notev(0, a, b);
    else if (kind === 'r') w.preview_preset(0, a);
    else if (kind === 'k') w.preview_panic();
  }
  process(_inputs, outputs) {
    const left = outputs[0][0], right = outputs[0][1] || outputs[0][0];
    let audio = new Float32Array(this.w.memory.buffer, this.ptr, this.frames * 2);
    for (let i = 0; i < left.length; i++) {
      if (this.pos >= this.frames) {
        this.ptr = this.w.preview_render();
        audio = new Float32Array(this.w.memory.buffer, this.ptr, this.frames * 2);
        this.pos = 0;
      }
      const l = audio[this.pos * 2], r = audio[this.pos * 2 + 1];
      left[i] = l;
      right[i] = r;
      const a = l < 0 ? -l : l;
      if (a > this.peak) this.peak = a;
      this.pos++;
    }
    this.count += left.length;
    if (this.count >= 2048) {
      this.port.postMessage({ peak: this.peak });
      this.count = 0;
      this.peak = 0;
    }
    return true;
  }
}
registerProcessor('selva', SelvaProcessor);
