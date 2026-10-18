// Selva1 · renders every factory sound offline with the same WebAssembly the browser runs, and reports its
// level, so the presets sit at a similar loudness.  node tools/render_presets.mjs [out-dir]
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";

const root = path.dirname(path.dirname(new URL(import.meta.url).pathname));
const out = process.argv[2] || path.join(root, "build", "presets");
fs.mkdirSync(out, { recursive: true });
const bytes = fs.readFileSync(path.join(root, "build", "selva1.wasm"));
const { instance } = await WebAssembly.instantiate(bytes, {});
const X = instance.exports;
const ctx = {};
vm.runInNewContext(fs.readFileSync(path.join(root, "web", "presets.js"), "utf8") + ";this.PRESETS = PRESETS;", ctx);
const PRESETS = ctx.PRESETS;
X.s1_init(48000);
const cstr = (p) => { const m = new Uint8Array(X.memory.buffer); let e = p; while (m[e]) e++; return new TextDecoder().decode(m.subarray(p, e)); };
const INFO = JSON.parse(cstr(X.s1_info()));
const BYKEY = Object.fromEntries(INFO.params.map((p) => [p.key, p]));
const LANES = ["gate", "note", "velo", "prob", "m1", "m2", "m3", "m4"];
const SR = 48000;

function apply(pr) {
  X.s1_init(SR);
  for (const k in pr.p) { if (!BYKEY[k]) throw new Error(pr.n + ": no " + k); X.s1_set(BYKEY[k].id, pr.p[k]); }
  (pr.m || []).forEach(([s, d, a], i) => {
    const si = INFO.sources.indexOf(s), di = INFO.dests.findIndex((x) => x.name === d);
    if (si < 0 || di < 0) throw new Error(pr.n + ": bad route " + s + " -> " + d);
    X.s1_set(BYKEY[`mod${i + 1}.src`].id, si);
    X.s1_set(BYKEY[`mod${i + 1}.dst`].id, di);
    X.s1_set(BYKEY[`mod${i + 1}.amt`].id, a);
  });
  for (const ln in pr.s || {}) {
    const l = LANES.indexOf(ln);
    pr.s[ln].forEach((v, i) => X.s1_set(INFO.seqBase + i * 8 + l, v));
  }
}
function render(secs, L, R, at) {
  const n = Math.round(secs * SR), pl = X.s1_buf_l(), pr = X.s1_buf_r();
  for (let i = 0; i < n; i += 128) {
    const m = Math.min(128, n - i);
    X.s1_render_io(m);
    const a = new Float32Array(X.memory.buffer, pl, m), b = new Float32Array(X.memory.buffer, pr, m);
    for (let k = 0; k < m; k++) { L.push(a[k]); R.push(b[k]); }
  }
}
function wav(file, L, R) {
  const n = L.length, buf = Buffer.alloc(44 + n * 4);
  buf.write("RIFF", 0); buf.writeUInt32LE(36 + n * 4, 4); buf.write("WAVEfmt ", 8); buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22); buf.writeUInt32LE(SR, 24); buf.writeUInt32LE(SR * 4, 28);
  buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34); buf.write("data", 36); buf.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) {
    buf.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(L[i] * 32767))), 44 + i * 4);
    buf.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(R[i] * 32767))), 46 + i * 4);
  }
  fs.writeFileSync(file, buf);
}
const all = { L: [], R: [] };
const rows = [], suggest = {}, TARGET = +(process.env.TARGET || -15);
PRESETS.forEach((pr, idx) => {
  apply(pr);
  const L = [], R = [];
  const seq = pr.s && pr.s.gate, arp = pr.p["arp.on"];
  if (seq) { X.s1_transport(1); render(4, L, R); X.s1_transport(0); render(1, L, R); }
  else if (arp) { [48, 55, 60, 63].forEach((n) => X.s1_note_on(n, 100)); render(3.5, L, R); [48, 55, 60, 63].forEach((n) => X.s1_note_off(n)); render(1.5, L, R); }
  else if (pr.b === "A") {
    const seqn = [48, 48, 51, 43, 46, 48];
    seqn.forEach((n, i) => { X.s1_note_on(n, i % 2 ? 90 : 120); render(0.32, L, R); X.s1_note_off(n); render(0.08, L, R); });
    X.s1_note_on(36, 110); render(1.2, L, R); X.s1_note_off(36); render(0.8, L, R);
  } else if (pr.b === "B") {
    [60, 63, 67, 70, 67].forEach((n, i) => { X.s1_note_on(n, 100); render(i === 4 ? 1.2 : 0.4, L, R); X.s1_note_off(n); render(0.05, L, R); });
    [60, 64, 67].forEach((n) => X.s1_note_on(n, 100)); render(1, L, R); [60, 64, 67].forEach((n) => X.s1_note_off(n)); render(1.5, L, R);
  } else {
    [48, 55, 60, 63].forEach((n) => X.s1_note_on(n, 100)); render(3.5, L, R); [48, 55, 60, 63].forEach((n) => X.s1_note_off(n)); render(2.5, L, R);
  }
  let pk = 0, acc = 0, cnt = 0, zc = 0, bad = 0;
  for (let i = 0; i < L.length; i++) {
    const a = Math.abs(L[i]);
    if (!Number.isFinite(L[i]) || !Number.isFinite(R[i])) bad++;
    pk = Math.max(pk, a, Math.abs(R[i]));
    acc += L[i] * L[i] + R[i] * R[i]; cnt += 2;
    if (i && (L[i] < 0) !== (L[i - 1] < 0)) zc++;
  }
  const rms = Math.sqrt(acc / cnt), db = 20 * Math.log10(rms + 1e-9);
  const W = SR * 0.4;
  let wmax = 0;
  for (let i = 0; i + W <= L.length; i += SR * 0.1) {
    let a2 = 0;
    for (let k = i; k < i + W; k++) a2 += L[k] * L[k] + R[k] * R[k];
    wmax = Math.max(wmax, Math.sqrt(a2 / (2 * W)));
  }
  const wdb = 20 * Math.log10(wmax + 1e-9), lvl = (pr.p["amp.level"] ?? 0.7) * Math.pow(10, (TARGET - wdb) / 40);
  suggest[pr.n] = +Math.min(1, lvl).toFixed(3);
  rows.push(`${String(idx + 1).padStart(3, "0")} ${pr.b} ${pr.n.padEnd(15)} peak ${pk.toFixed(3)}  rms ${db.toFixed(1).padStart(6)} dB  loud ${wdb.toFixed(1).padStart(6)} dB  amp ${suggest[pr.n]}  zc/s ${Math.round(zc / (L.length / SR))}${bad ? "  BAD " + bad : ""}`);
  wav(path.join(out, `${String(idx + 1).padStart(3, "0")}.wav`), L, R);
  for (let i = 0; i < L.length; i++) { all.L.push(L[i]); all.R.push(R[i]); }
  for (let i = 0; i < SR * 0.3; i++) { all.L.push(0); all.R.push(0); }
});
console.log(rows.join("\n"));
wav(path.join(out, "selva1-todos-os-sons.wav"), all.L, all.R);
fs.writeFileSync(path.join(out, "suggest.json"), JSON.stringify(suggest, null, 1));
console.log("wrote", out);
