/* Selva1 · the panel. Every control is built from the engine's own parameter list (s1_info), so the panel,
 * the presets and the engine always agree. The engine runs twice: on the audio thread (the sound) and here
 * (the wavetables for the displays, the defaults). Copyright (C) 2026 DeAbrew. */
"use strict";
(function () {
  const $ = (s, r) => (r || document).querySelector(s);
  const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
  const K_LIN = 0, K_EXP = 1, K_INT = 2, K_ENUM = 3, K_BOOL = 4, K_BIP = 5;
  const LANES = ["gate", "note", "velo", "prob", "m1", "m2", "m3", "m4"];
  const LANE_LABEL = ["GATE", "NOTE", "VELO", "PROB", "MOD 1", "MOD 2", "MOD 3", "MOD 4"];
  const LANE_DEF = [0, 0, 0.8, 1, 0, 0, 0, 0];
  const BIPOLAR = new Set(["LFO 1", "LFO 2", "LFO 3", "KEY", "BEND", "RANDOM"]);
  const BANKS = { A: "CONCRETE", B: "ASFALTO", C: "GAROA", D: "USINA", U: "USUÁRIO" };
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const SVGNS = "http://www.w3.org/2000/svg";
  const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
  const pad3 = (n) => String(n).padStart(3, "0");
  const mem = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* private window: fine */ } },
  };
  const noteName = (n) => NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 2);   /* Ableton: 60 = C3 */

  /* ------------------------------------------------------------------ the engine, here --- */
  function b64(s) { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; }
  const WASM = b64(WASM_B64);
  let X = null, INFO, P, BYKEY = {}, SEQ0, NALL, DEFS, VALUES, DKEY = {}, NSRC;
  const LIS = new Map();
  let loading = false;

  function cstr(ptr) {
    const m = new Uint8Array(X.memory.buffer);
    let e = ptr;
    while (m[e]) e++;
    return new TextDecoder().decode(m.subarray(ptr, e));
  }
  const seqId = (step, lane) => SEQ0 + step * 8 + lane;
  const id = (key) => BYKEY[key].id;
  const val = (key) => VALUES[BYKEY[key].id];

  function norm(p, v) {
    if (p.max <= p.min) return 0;
    if (p.kind === K_EXP) return clamp(Math.log(Math.max(v, p.min) / p.min) / Math.log(p.max / p.min), 0, 1);
    return clamp((v - p.min) / (p.max - p.min), 0, 1);
  }
  function denorm(p, n) {
    n = clamp(n, 0, 1);
    if (p.kind === K_EXP) return p.min * Math.pow(p.max / p.min, n);
    return p.min + n * (p.max - p.min);
  }
  function quant(i, v) {
    if (i >= SEQ0) return (i - SEQ0) % 8 === 1 ? clamp(Math.round(v), -24, 24) : clamp(v, 0, 1);
    const p = P[i];
    if (p.kind === K_INT || p.kind === K_ENUM || p.kind === K_BOOL) v = Math.round(v);
    return clamp(v, p.min, p.max);
  }
  function on(i, fn) {
    if (!LIS.has(i)) LIS.set(i, []);
    LIS.get(i).push(fn);
    fn(VALUES[i]);
  }
  function set(i, v) {
    v = quant(i, v);
    if (VALUES[i] === v) return;
    VALUES[i] = v;
    send(i, v);
    const ls = LIS.get(i);
    if (ls) for (const f of ls) f(v);
    if (!loading) touched();
    if (i < SEQ0 && /^mod\d+\./.test(P[i].key)) later(modRings);
  }
  const queued = new Set();
  function later(fn) {
    if (queued.has(fn)) return;
    queued.add(fn);
    requestAnimationFrame(() => { queued.delete(fn); fn(); });
  }

  function fmtTime(s) {
    if (s < 0.1) return (s * 1000).toFixed(s < 0.01 ? 1 : 0) + " ms";
    return s < 10 ? s.toFixed(2) + " s" : s.toFixed(1) + " s";
  }
  function fmtHz(h) {
    if (h >= 1000) return (h / 1000).toFixed(h >= 10000 ? 1 : 2) + " kHz";
    return h >= 100 ? h.toFixed(0) + " Hz" : h.toFixed(h >= 10 ? 1 : 2) + " Hz";
  }
  function fmt(p, v) {
    const k = p.key;
    if (p.opts) return p.opts[Math.round(v)] || "";
    if (k === "voice.glide") return v <= 0.0011 ? "OFF" : fmtTime(v);
    if (/^env\d\.[adr]$/.test(k)) return fmtTime(v);
    if (k === "flt.cutoff" || /^lfo\d\.rate$/.test(k) || k === "fx.cho.rate") return fmtHz(v);
    if (/\.pan$/.test(k)) return Math.abs(v) < 0.005 ? "C" : (v < 0 ? "L " : "R ") + Math.round(Math.abs(v) * 100);
    if (/\.fine$/.test(k) || k === "global.tune") return (v > 0 ? "+" : "") + Math.round(v) + " ct";
    if (k === "global.bpm") return Math.round(v) + " BPM";
    if (k === "voice.bend") return "±" + Math.round(v) + " st";
    if (k === "arp.oct") return Math.round(v) + " OCT";
    if (p.kind === K_INT) return (p.min < 0 && v > 0 ? "+" : "") + Math.round(v);
    if (p.kind === K_BIP) return (v > 0 ? "+" : "") + Math.round(v * 100);
    return Math.round(v * 100) + "%";
  }

  /* ------------------------------------------------------------------ the audio thread --- */
  let ctx = null, node = null, starting = null, UI = null;
  let pending = [];
  const held = new Map();                    /* notes held by the panel, the keyboard, MIDI: note -> velocity */
  function send(i, v) {
    if (!node) return;
    pending.push(i, v);
    if (pending.length === 2) queueMicrotask(flush);
  }
  function flush() { if (node && pending.length) node.port.postMessage({ t: "set", a: pending }); pending = []; }
  function post(m) { if (node) node.port.postMessage(m); }
  function sendAll() {
    const a = [];
    for (let i = 0; i < NALL; i++) a.push(i, VALUES[i]);
    node.port.postMessage({ t: "set", a });
  }
  async function addWorklet(c) {
    const tries = [];
    if (/^https?:$/.test(location.protocol)) tries.push(() => c.audioWorklet.addModule("selva1-worklet.js"));
    tries.push(() => c.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET_SRC], { type: "text/javascript" }))));
    tries.push(() => c.audioWorklet.addModule("data:text/javascript;charset=utf-8," + encodeURIComponent(WORKLET_SRC)));
    let err;
    for (const t of tries) {
      try { await t(); return; } catch (e) { err = e; }
    }
    throw err;
  }
  function startAudio() {
    if (ctx && node) {
      if (ctx.state !== "running") ctx.resume().then(powerLabel, powerLabel);
      return Promise.resolve();
    }
    if (starting) return starting;
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC || !("AudioWorkletNode" in window)) { toast("Este navegador não tem AudioWorklet. Use Chrome, Edge, Firefox ou Safari atual."); return Promise.reject(); }
    ctx = new AC({ latencyHint: "interactive" });
    starting = addWorklet(ctx).then(() => {
      node = new AudioWorkletNode(ctx, "selva1", {
        numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
        processorOptions: { wasm: WASM.buffer.slice(WASM.byteOffset, WASM.byteOffset + WASM.byteLength) },
      });
      node.port.onmessage = (e) => { if (e.data.t === "ui") UI = e.data.s; };
      node.connect(ctx.destination);
      sendAll();
      post({ t: "cc", c: 64, v: sustainOn ? 127 : 0 });
      for (const [n, v] of held) post({ t: "on", n, v });
      if (wantPlay) post({ t: "play", v: 1 });
      powerLabel();
      ctx.onstatechange = powerLabel;
    }).catch((e) => {
      console.error(e);
      toast("Não consegui ligar o áudio: " + (e && e.message ? e.message : e));
      ctx = null; starting = null;
    });
    return starting;
  }
  function powerLabel() {
    const onNow = ctx && node && ctx.state === "running";
    $("#power").classList.toggle("on", !!onNow);
    $("#powerlbl").textContent = onNow ? "SOM LIGADO" : "LIGAR O SOM";
  }

  /* ------------------------------------------------------------------ toast --- */
  let toastT = 0;
  function toast(msg) {
    const t = $("#toast");
    t.textContent = msg;
    t.hidden = false;
    clearTimeout(toastT);
    toastT = setTimeout(() => { t.hidden = true; }, 2600);
  }

  /* ------------------------------------------------------------------ worn knobs --- */
  function rng(seed) {
    let s = (seed * 2654435761) >>> 0 || 1;
    return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  }
  /* one knob, drawn once as a picture and turned as a whole: a knurled bakelite skirt with sand in the
   * grooves, an aluminium cap gone dull, rust pitting, dust, the pointer's paint chipped */
  function knobTexture(seed) {
    const S = 192, c = S / 2, R = c - 1, cv = document.createElement("canvas");
    cv.width = cv.height = S;
    const g = cv.getContext("2d"), r = rng(seed + 11);
    const pt = (a, d) => [c + Math.sin(a) * d, c - Math.cos(a) * d];
    let gr = g.createRadialGradient(c, c, R * 0.45, c, c, R);
    gr.addColorStop(0, "#24211e");
    gr.addColorStop(1, "#0d0c0b");
    g.fillStyle = gr;
    g.beginPath(); g.arc(c, c, R, 0, 7); g.fill();
    const N = 64;
    for (let i = 0; i < N; i++) {
      const a = (i / N) * Math.PI * 2, [x1, y1] = pt(a, R * 0.79), [x2, y2] = pt(a, R * 0.99);
      g.strokeStyle = i % 2 ? "rgba(84,77,69,.5)" : "rgba(0,0,0,.65)";
      g.lineWidth = 2.6;
      g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.stroke();
    }
    for (let i = 0; i < 3; i++) {               /* the skirt rubbed brown where fingers go */
      const [x, y] = pt(r() * 6.283, R * 0.9), rr = 10 + r() * 16, rg = g.createRadialGradient(x, y, 0, x, y, rr);
      rg.addColorStop(0, "rgba(122,94,66,.22)");
      rg.addColorStop(1, "rgba(122,94,66,0)");
      g.fillStyle = rg;
      g.beginPath(); g.arc(x, y, rr, 0, 7); g.fill();
    }
    for (let i = 0; i < 560; i++) {             /* sand caught in the knurl */
      const [x, y] = pt(r() * 6.283, R * (0.77 + Math.pow(r(), 0.55) * 0.22)), s = 0.5 + r() * 1.5;
      g.fillStyle = `rgba(${(198 + r() * 34) | 0},${(168 + r() * 30) | 0},${(118 + r() * 34) | 0},${0.16 + r() * 0.5})`;
      g.fillRect(x, y, s, s);
    }
    g.fillStyle = "#060606";
    g.beginPath(); g.arc(c, c, R * 0.765, 0, 7); g.fill();
    const Rc = R * 0.72;
    gr = g.createRadialGradient(c, c, 0, c, c, Rc);
    const tint = r();
    gr.addColorStop(0, tint < 0.5 ? "#a59d8f" : "#a39782");
    gr.addColorStop(0.7, tint < 0.5 ? "#8b8376" : "#867a66");
    gr.addColorStop(1, "#5f584e");
    g.fillStyle = gr;
    g.beginPath(); g.arc(c, c, Rc, 0, 7); g.fill();
    g.save();
    g.beginPath(); g.arc(c, c, Rc, 0, 7); g.clip();
    for (let rr = 1; rr < Rc; rr += 0.75) {     /* turned aluminium */
      g.strokeStyle = r() < 0.5 ? `rgba(255,250,240,${r() * 0.07})` : `rgba(0,0,0,${r() * 0.08})`;
      g.lineWidth = 0.8;
      g.beginPath(); g.arc(c, c, rr, 0, 7); g.stroke();
    }
    const cols = ["#6b3519", "#8B4A2B", "#a35e36", "#4a2412", "#b5733f", "#7a4a2a"];
    const rusty = r(), nb = 3 + ((rusty * 7) | 0);
    for (let b = 0; b < nb; b++) {               /* rust: some knobs more than others */
      const [bx, by] = pt(r() * 6.283, Rc * (0.25 + r() * 0.8)), br = 6 + r() * (12 + rusty * 22);
      const rg = g.createRadialGradient(bx, by, 0, bx, by, br);
      rg.addColorStop(0, "rgba(118,60,30,.7)");
      rg.addColorStop(0.6, "rgba(139,74,43,.3)");
      rg.addColorStop(1, "rgba(139,74,43,0)");
      g.fillStyle = rg;
      g.beginPath(); g.arc(bx, by, br, 0, 7); g.fill();
      for (let i = 0; i < br * 22; i++) {
        const a = r() * 6.283, d = Math.pow(r(), 0.7) * br, s = 0.6 + r() * 1.9;
        g.globalAlpha = 0.2 + r() * 0.6;
        g.fillStyle = cols[(r() * cols.length) | 0];
        g.fillRect(bx + Math.sin(a) * d, by - Math.cos(a) * d, s, s);
      }
      g.globalAlpha = 1;
    }
    gr = g.createRadialGradient(c, c, Rc * 0.62, c, c, Rc);   /* grime at the rim */
    gr.addColorStop(0, "rgba(44,32,20,0)");
    gr.addColorStop(1, "rgba(44,32,20,.62)");
    g.fillStyle = gr;
    g.fillRect(0, 0, S, S);
    gr = g.createLinearGradient(0, c + Rc * 0.2, 0, c + Rc);   /* sand settled on the lower side */
    gr.addColorStop(0, "rgba(191,162,122,0)");
    gr.addColorStop(1, `rgba(191,162,122,${0.12 + r() * 0.18})`);
    g.fillStyle = gr;
    g.fillRect(0, 0, S, S);
    for (let i = 0; i < 1100; i++) {            /* dust and sand */
      const [x, y] = pt(r() * 6.283, Math.pow(r(), 0.4) * Rc), s = 0.4 + r() * (r() < 0.1 ? 2.2 : 1.1);
      g.fillStyle = `rgba(${(200 + r() * 40) | 0},${(172 + r() * 34) | 0},${(126 + r() * 40) | 0},${0.08 + r() * 0.4})`;
      g.fillRect(x, y, s, s);
    }
    for (let i = 0; i < 5; i++) {               /* scratches */
      const [x, y] = pt(r() * 6.283, r() * Rc), a = r() * 6.283, l = 8 + r() * 30;
      g.strokeStyle = `rgba(240,232,215,${0.08 + r() * 0.14})`;
      g.lineWidth = 0.7;
      g.beginPath(); g.moveTo(x, y); g.lineTo(x + Math.cos(a) * l, y + Math.sin(a) * l); g.stroke();
    }
    g.restore();
    g.lineCap = "butt";
    g.strokeStyle = "rgba(14,12,10,.85)";       /* the engraved pointer on the cap */
    g.lineWidth = 5;
    g.beginPath(); g.moveTo(c, c - Rc * 0.12); g.lineTo(c, c - Rc * 0.99); g.stroke();
    g.strokeStyle = "#ebe1ca";                  /* and painted on the skirt */
    g.lineWidth = 5.5;
    g.beginPath(); g.moveTo(c, c - R * 0.79); g.lineTo(c, c - R * 0.985); g.stroke();
    for (let i = 0; i < 16; i++) {
      g.fillStyle = `rgba(30,24,18,${0.3 + r() * 0.6})`;
      const s = 0.6 + r() * 1.6;
      g.fillRect(c - 2.6 + r() * 5, c - R * (0.79 + r() * 0.2), s, s);
    }
    return cv.toDataURL("image/png");
  }
  let TEX = [];
  function sharedDefs() {
    const s = document.createElementNS(SVGNS, "svg");
    s.setAttribute("width", "0");
    s.setAttribute("height", "0");
    s.setAttribute("aria-hidden", "true");
    s.style.position = "absolute";
    s.innerHTML = `<defs>
      <radialGradient id="klight" cx="34%" cy="26%" r="80%"><stop offset="0" stop-color="#fff6e4" stop-opacity=".24"/>
        <stop offset=".42" stop-color="#fff6e4" stop-opacity=".05"/><stop offset=".8" stop-color="#000" stop-opacity=".18"/>
        <stop offset="1" stop-color="#000" stop-opacity=".5"/></radialGradient>
      <radialGradient id="kshadow"><stop offset=".55" stop-color="#000" stop-opacity=".7"/><stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>
    </defs>`;
    document.body.prepend(s);
  }
  function arcPath(cx, cy, r, a0, a1) {
    if (Math.abs(a1 - a0) < 0.05) return "";
    if (a1 < a0) { const t = a0; a0 = a1; a1 = t; }
    const p = (a) => [cx + r * Math.sin((a * Math.PI) / 180), cy - r * Math.cos((a * Math.PI) / 180)];
    const [x0, y0] = p(a0), [x1, y1] = p(a1);
    return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
  }

  /* ------------------------------------------------------------------ controls --- */
  const SIZES = { S: 36, M: 46, L: 60 };
  const KNOBS = new Map();                   /* key -> [knob] */
  let knobSeed = 1;
  function knob(el, p) {
    const s = SIZES[el.dataset.size || "M"], pad = 11, W = s + pad * 2, c = W / 2, Rk = s / 2;
    const bip = p.kind === K_BIP;
    const label = el.dataset.label || p.label;
    const seed = knobSeed++, tex = TEX[seed % TEX.length], flip = seed % 3 === 1;
    let ticks = "";
    for (let i = 0; i <= 10; i++) {
      const a = ((-135 + i * 27) * Math.PI) / 180, r1 = Rk + 7.5, r2 = Rk + (i % 5 ? 9.5 : 11);
      ticks += `M${(c + r1 * Math.sin(a)).toFixed(2)} ${(c - r1 * Math.cos(a)).toFixed(2)}L${(c + r2 * Math.sin(a)).toFixed(2)} ${(c - r2 * Math.cos(a)).toFixed(2)}`;
    }
    el.innerHTML = `<svg width="${W}" height="${W}" viewBox="0 0 ${W} ${W}" aria-hidden="true">
      <path d="${ticks}" stroke="#d9cdb3" stroke-opacity=".42" stroke-width="1.1" fill="none"/>
      <path d="${arcPath(c, c, Rk + 2.6, -135, 135)}" stroke="#2c2a27" stroke-width="2.2" fill="none"/>
      <path class="km" stroke="#BFA27A" stroke-width="1.6" fill="none"/>
      <path class="ka" stroke="#C63F3E" stroke-width="2.2" fill="none"/>
      <circle cx="${c + 1.6}" cy="${c + 2.6}" r="${Rk * 1.12}" fill="url(#kshadow)"/>
      <g class="kr"><image href="${tex}" x="${c - Rk}" y="${c - Rk}" width="${s}" height="${s}"${flip ? ` transform="translate(${2 * c} 0) scale(-1 1)"` : ""}/></g>
      <circle cx="${c}" cy="${c}" r="${Rk}" fill="url(#klight)" pointer-events="none"/>
    </svg><span class="kl">${label}</span><span class="kv"></span>`;
    el.tabIndex = 0;
    el.setAttribute("role", "slider");
    el.setAttribute("aria-label", label);
    el.setAttribute("aria-valuemin", "0");
    el.setAttribute("aria-valuemax", "100");
    const rot = $(".kr", el), arc = $(".ka", el), marc = $(".km", el), kv = $(".kv", el), kl = $(".kl", el);
    const k = {
      el, p,
      setLabel(t) { kl.textContent = t; el.setAttribute("aria-label", t); },
      setMod(lo, hi) {
        if (lo === null) { marc.setAttribute("d", ""); return; }
        marc.setAttribute("d", arcPath(c, c, Rk + 5.4, -135 + 270 * clamp(lo, 0, 1), -135 + 270 * clamp(hi, 0, 1)));
      },
    };
    on(p.id, (v) => {
      const n = norm(p, v), a = -135 + 270 * n;
      rot.setAttribute("transform", `rotate(${a.toFixed(2)} ${c} ${c})`);
      arc.setAttribute("d", bip ? arcPath(c, c, Rk + 2.6, 0, a) : arcPath(c, c, Rk + 2.6, -135, a));
      kv.textContent = fmt(p, v);
      el.setAttribute("aria-valuenow", Math.round(n * 100));
      el.setAttribute("aria-valuetext", fmt(p, v));
    });
    dragValue(el, p, () => norm(p, VALUES[p.id]), (n) => set(p.id, denorm(p, n)), 0.0055);
    el.addEventListener("dblclick", () => set(p.id, p.def));
    if (!KNOBS.has(p.key)) KNOBS.set(p.key, []);
    KNOBS.get(p.key).push(k);
    return k;
  }
  /* vertical drag (and wheel, arrows) on a 0..1 control; SHIFT is fine */
  function dragValue(el, p, get, put, sens) {
    let y0 = 0, n0 = 0, idp = null;
    el.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      el.focus({ preventScroll: true });
      idp = e.pointerId;
      el.setPointerCapture(idp);
      y0 = e.clientY; n0 = get();
      el.classList.add("drag");
    });
    el.addEventListener("pointermove", (e) => {
      if (e.pointerId !== idp) return;
      const d = (y0 - e.clientY) * (e.shiftKey ? sens * 0.2 : sens);
      put(clamp(n0 + d, 0, 1));
    });
    const end = (e) => { if (e.pointerId !== idp) return; idp = null; el.classList.remove("drag"); };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);
    el.addEventListener("wheel", (e) => {
      if (document.activeElement !== el) return;   /* the page scrolls; a knob you clicked turns */
      e.preventDefault();
      const step = p && (p.kind === K_INT || p.kind === K_ENUM) ? 1 / Math.max(1, p.max - p.min) : e.shiftKey ? 0.004 : 0.02;
      put(clamp(get() + (e.deltaY < 0 ? step : -step), 0, 1));
    }, { passive: false });
    el.addEventListener("keydown", (e) => {
      const step = p && (p.kind === K_INT || p.kind === K_ENUM) ? 1 / Math.max(1, p.max - p.min) : e.shiftKey ? 0.1 : 0.01;
      let n = null;
      if (e.key === "ArrowUp" || e.key === "ArrowRight") n = get() + step;
      else if (e.key === "ArrowDown" || e.key === "ArrowLeft") n = get() - step;
      else if (e.key === "Home") n = 0;
      else if (e.key === "End") n = 1;
      else if ((e.key === "Delete" || e.key === "Backspace") && p) { set(p.id, p.def); e.preventDefault(); e.stopPropagation(); return; }
      if (n !== null) { e.preventDefault(); e.stopPropagation(); put(clamp(n, 0, 1)); }
    });
  }
  function stepper(el, p) {
    const label = el.dataset.label || p.label;
    el.innerHTML = `<div class="box" tabindex="0" role="spinbutton" aria-label="${label}"></div><span class="lbl">${label}</span>`;
    const box = $(".box", el);
    on(p.id, (v) => {
      box.textContent = fmt(p, v);
      box.setAttribute("aria-valuenow", v);
    });
    dragValue(box, p, () => norm(p, VALUES[p.id]), (n) => set(p.id, denorm(p, n)), 1 / (14 * Math.max(1, p.max - p.min)));
    box.addEventListener("dblclick", () => set(p.id, p.def));
    if (!KNOBS.has(p.key)) KNOBS.set(p.key, []);
  }
  function select(el, p) {
    let opts;
    if (/^mod\d+\.src$/.test(p.key)) opts = INFO.sources;
    else if (/^mod\d+\.dst$/.test(p.key)) opts = INFO.dests.map((d) => d.name);
    else if (p.opts) opts = p.opts;
    else { opts = []; for (let i = p.min; i <= p.max; i++) opts.push(String(i)); }
    const base = p.opts || /^mod/.test(p.key) ? 0 : p.min;
    el.innerHTML = opts.map((o, i) => `<option value="${i + base}">${o}</option>`).join("");
    if (!el.getAttribute("aria-label")) el.setAttribute("aria-label", p.label);
    on(p.id, (v) => { el.value = String(Math.round(v)); });
    el.addEventListener("change", () => set(p.id, +el.value));
  }
  function toggle(el, p) {
    if (!el.type) el.type = "button";
    on(p.id, (v) => el.setAttribute("aria-pressed", v ? "true" : "false"));
    el.addEventListener("click", () => set(p.id, VALUES[p.id] ? 0 : 1));
  }
  const ICONS = {
    lfo: ["M1 6Q5.5-2 10 6T19 6", "M1 10L10 2L19 10", "M1 10L10 2V10L19 2", "M1 2L10 10V2L19 10", "M1 10V2H10V10H19V2",
      "M1 8H5V3H9V10H13V5H17V7H19", "M1 7C5 2 8 11 12 6S17 3 19 5"],
    sub: ["M1 6Q5.5-2 10 6T19 6", "M1 10V2H10V10H19V2"],
  };
  function seg(el, p) {
    const icons = el.dataset.icons && ICONS[el.dataset.icons];
    const opts = p.opts || ["OFF", "ON"];
    el.setAttribute("role", "group");
    el.setAttribute("aria-label", p.label);
    el.innerHTML = opts.map((o, i) => `<button type="button" data-v="${i}" ${icons ? `aria-label="${o}" title="${o}"` : ""}>${icons ? `<svg viewBox="0 0 20 12"><path d="${icons[i]}"/></svg>` : o}</button>`).join("");
    const bs = $$("button", el);
    on(p.id, (v) => bs.forEach((b, i) => b.setAttribute("aria-pressed", i === Math.round(v) ? "true" : "false")));
    bs.forEach((b, i) => b.addEventListener("click", () => set(p.id, i)));
  }
  function amount(el, p) {
    const fill = $("i", el), txt = $("span", el);
    el.setAttribute("role", "slider");
    el.tabIndex = 0;
    el.setAttribute("aria-label", "Quantidade");
    on(p.id, (v) => {
      const a = 50 + v * 50;
      fill.style.left = Math.min(50, a) + "%";
      fill.style.width = Math.abs(a - 50) + "%";
      txt.textContent = (v > 0 ? "+" : "") + Math.round(v * 100);
      el.setAttribute("aria-valuenow", Math.round(v * 100));
    });
    let idp = null;
    const at = (e) => { const r = el.getBoundingClientRect(); let v = ((e.clientX - r.left) / r.width) * 2 - 1; if (Math.abs(v) < 0.03) v = 0; set(p.id, clamp(v, -1, 1)); };
    el.addEventListener("pointerdown", (e) => { idp = e.pointerId; el.setPointerCapture(idp); at(e); });
    el.addEventListener("pointermove", (e) => { if (e.pointerId === idp) at(e); });
    el.addEventListener("pointerup", () => { idp = null; });
    el.addEventListener("dblclick", () => set(p.id, 0));
    el.addEventListener("keydown", (e) => {
      const d = e.key === "ArrowRight" || e.key === "ArrowUp" ? 0.02 : e.key === "ArrowLeft" || e.key === "ArrowDown" ? -0.02 : 0;
      if (d) { e.preventDefault(); e.stopPropagation(); set(p.id, clamp(VALUES[p.id] + d * (e.shiftKey ? 5 : 1), -1, 1)); }
    });
  }
  function bind(root) {
    $$("[data-k]", root).forEach((el) => {
      if (el.dataset.b) return;
      const p = BYKEY[el.dataset.k];
      if (!p) { console.warn("no parameter", el.dataset.k); return; }
      el.dataset.b = "1";
      if (el.classList.contains("knob")) knob(el, p);
      else if (el.classList.contains("stepper")) stepper(el, p);
      else if (el.tagName === "SELECT") select(el, p);
      else if (el.classList.contains("led") || el.classList.contains("seg1")) toggle(el, p);
      else if (el.classList.contains("seg") || el.classList.contains("mtabs")) seg(el, p);
      else if (el.classList.contains("amt")) amount(el, p);
    });
    $$("[data-power]", root).forEach((sec) => {
      if (sec.dataset.pb) return;
      sec.dataset.pb = "1";
      on(id(sec.dataset.power), (v) => sec.classList.toggle("off", !v));
    });
  }

  /* ------------------------------------------------------------------ canvases --- */
  function prep(cv) {
    const dpr = Math.min(2, window.devicePixelRatio || 1), w = cv.clientWidth, h = cv.clientHeight;
    if (!w || !h) return null;
    const W = Math.round(w * dpr), H = Math.round(h * dpr);
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    const g = cv.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    cv.cw = w; cv.ch = h;
    return g;
  }
  function fixHeight(cv) { cv.style.height = cv.getAttribute("height") + "px"; }
  function grid(g, W, H) {
    g.strokeStyle = "#191817";
    g.lineWidth = 1;
    g.beginPath();
    for (let i = 1; i < 8; i++) { const x = Math.round((W * i) / 8) + 0.5; g.moveTo(x, 0); g.lineTo(x, H); }
    for (let i = 1; i < 4; i++) { const y = Math.round((H * i) / 4) + 0.5; g.moveTo(0, y); g.lineTo(W, y); }
    g.stroke();
  }
  const WTC = {};
  function wtFrames(t) {
    if (WTC[t]) return WTC[t];
    const NF = INFO.frames, NS = 256, out = new Float32Array(NF * NS), ptr = X.s1_buf_tmp();
    for (let f = 0; f < NF; f++) {
      X.s1_wt_frame(t, f, ptr, NS);
      out.set(new Float32Array(X.memory.buffer, ptr, NS), f * NS);
    }
    return (WTC[t] = out);
  }

  /* ------------------------------------------------------------------ the oscillators --- */
  const OSCS = [];
  const POSLABEL = ["WT POS", "SHAPE", "INDEX", "COLOR"];
  function buildOsc(sec) {
    const o = sec.dataset.osc, L = o.toUpperCase(), pre = `osc.${o}.`;
    sec.dataset.power = pre + "on";
    sec.innerHTML = `
      <div class="mh"><span class="no">${L}</span><h2>Osc</h2><button class="led" data-k="${pre}on" aria-label="Oscilador ${L} ligado"></button>
        <span class="sp"></span><div class="mtabs" data-k="${pre}mode"></div></div>
      <div class="body page">
        <div class="screen oscscreen"><canvas height="122"></canvas>
          <button class="nav l" type="button" aria-label="Anterior">&#8249;</button><button class="nav r" type="button" aria-label="Próximo">&#8250;</button>
          <div class="cap"><span class="cn"></span><b class="cm"></b></div>
          <div class="tr"><span class="frame lbl"></span><select class="sel" data-k="${pre}wave" aria-label="Forma de onda"></select>
            <select class="sel" data-k="${pre}ratio" aria-label="Razão do FM"></select></div>
        </div>
        <div class="oscgrid">
          <div class="knob big" data-k="${pre}pos" data-size="L" data-label="WT POS"></div>
          <div class="stepper" data-k="${pre}oct" data-label="OCT"></div>
          <div class="stepper" data-k="${pre}semi" data-label="SEMI"></div>
          <div class="knob" data-k="${pre}fine" data-size="S" data-label="FINE"></div>
          <div class="stepper" data-k="${pre}unison" data-label="UNISON"></div>
          <div class="knob" data-k="${pre}detune" data-size="S" data-label="DETUNE"></div>
          <div class="knob" data-k="${pre}width" data-size="S" data-label="WIDTH"></div>
          <div class="knob" data-k="${pre}pan" data-size="S" data-label="PAN"></div>
          <div class="knob" data-k="${pre}level" data-label="LEVEL"></div>
        </div>
      </div>`;
    const cv = $("canvas", sec);
    fixHeight(cv);
    OSCS.push({ o, sec, cv, pre, live: -1 });
  }
  function oscWire(os) {
    const { sec, cv, pre } = os;
    const mode = id(pre + "mode"), table = id(pre + "table"), wave = id(pre + "wave"), ratio = id(pre + "ratio"), pos = id(pre + "pos");
    const posKnob = KNOBS.get(pre + "pos")[0];
    const wsel = $(`[data-k="${pre}wave"]`, sec), rsel = $(`[data-k="${pre}ratio"]`, sec), frame = $(".frame", sec);
    const cn = $(".cn", sec), cm = $(".cm", sec), navs = $$(".nav", sec);
    const NT = INFO.tables.length;
    const refresh = () => {
      const m = VALUES[mode];
      posKnob.setLabel(POSLABEL[m]);
      wsel.hidden = m !== 1;
      rsel.hidden = m !== 2;
      frame.hidden = m !== 0;
      navs.forEach((b) => { b.hidden = m === 3; });
      if (m === 0) { const t = Math.min(NT - 1, VALUES[table]); cn.textContent = pad3(t + 1); cm.textContent = INFO.tables[t]; }
      else if (m === 1) { cn.textContent = "ANALOG"; cm.textContent = BYKEY[pre + "wave"].opts[VALUES[wave]]; }
      else if (m === 2) { cn.textContent = "FM"; cm.textContent = "1 : " + BYKEY[pre + "ratio"].opts[VALUES[ratio]]; }
      else { cn.textContent = "NOISE"; cm.textContent = VALUES[pos] < 0.33 ? "BRANCO" : VALUES[pos] < 0.66 ? "ROSA" : "ESCURO"; }
      os.live = -1;
      drawOsc(os);
    };
    [mode, table, wave, ratio, pos].forEach((i) => on(i, refresh));
    navs[0].addEventListener("click", () => stepOsc(-1));
    navs[1].addEventListener("click", () => stepOsc(1));
    function stepOsc(d) {
      const m = VALUES[mode];
      if (m === 0) set(table, (VALUES[table] + d + NT) % NT);
      else if (m === 1) set(wave, (VALUES[wave] + d + 6) % 6);
      else if (m === 2) set(ratio, (VALUES[ratio] + d + 12) % 12);
    }
    const pp = BYKEY[pre + "pos"];
    dragValue(cv, pp, () => VALUES[pos], (n) => set(pos, n), 0.006);
    cv.style.cursor = "ns-resize";
    cv.setAttribute("aria-label", "Arraste para mudar a posição");
  }
  /* the oscillator's position after the matrix, from the sources the panel can see */
  function liveSources() {
    const s = new Float32Array(NSRC);
    if (UI) {
      for (let k = 0; k < 3; k++) { s[1 + k] = UI[8 + k]; s[4 + k] = UI[5 + k]; }
    }
    for (let k = 0; k < 4; k++) s[12 + k] = VALUES[id("macro" + (k + 1))];
    return s;
  }
  function liveNorm(key, s) {
    const p = BYKEY[key], d = DKEY[key];
    let n = norm(p, VALUES[p.id]);
    if (d === undefined) return n;
    for (let m = 1; m <= 16; m++) {
      if (VALUES[id(`mod${m}.dst`)] === d) n += VALUES[id(`mod${m}.amt`)] * s[VALUES[id(`mod${m}.src`)]];
    }
    return clamp(n, 0, 1);
  }
  function drawOsc(os, live) {
    const g = prep(os.cv);
    if (!g) return;
    const W = os.cv.cw, H = os.cv.ch, pre = os.pre, m = VALUES[id(pre + "mode")], pos = VALUES[id(pre + "pos")];
    const lp = live === undefined ? pos : live;
    grid(g, W, H);
    if (m === 0) {
      const t = Math.min(INFO.tables.length - 1, VALUES[id(pre + "table")]), fr = wtFrames(t), NF = INFO.frames, NS = 256;
      const dx = W * 0.15, dy = H * 0.36, w = W - dx - 30, x0 = 14, yb = H * 0.6, amp = H * 0.2;
      const line = (arr, off, ox, oy) => {
        g.beginPath();
        for (let i = 0; i < NS; i += 2) {
          const x = ox + (i / (NS - 1)) * w, y = oy - arr[off + i] * amp;
          i ? g.lineTo(x, y) : g.moveTo(x, y);
        }
        g.stroke();
      };
      g.lineWidth = 1;
      for (let f = NF - 1; f >= 0; f -= 1) {
        const q = f / (NF - 1);
        g.strokeStyle = `rgba(150,143,134,${0.1 + 0.2 * (1 - q)})`;
        line(fr, f * NS, x0 + dx * q, yb - dy * q);
      }
      const drawAt = (pn, color, lw) => {
        const fp = pn * (NF - 1), fa = Math.min(NF - 2, Math.floor(fp)), ff = fp - fa, cur = new Float32Array(NS);
        for (let i = 0; i < NS; i++) cur[i] = fr[fa * NS + i] * (1 - ff) + fr[(fa + 1) * NS + i] * ff;
        g.strokeStyle = color;
        g.lineWidth = lw;
        line(cur, 0, x0 + dx * pn, yb - dy * pn);
      };
      if (Math.abs(lp - pos) > 0.004) drawAt(pos, "rgba(191,162,122,.7)", 1);
      drawAt(lp, "#C63F3E", 2);
      $(".frame", os.sec).textContent = "FRAME " + String(Math.round(lp * (NF - 1)) + 1).padStart(2, "0");
      return;
    }
    const N = 300, ys = new Float32Array(N);
    const r = rng(7);
    let lpst = 0;
    for (let i = 0; i < N; i++) {
      const ph = ((i / N) * 2) % 1;
      let s = 0;
      if (m === 1) {
        const w = VALUES[id(pre + "wave")], pw = w === 1 ? 0.5 - 0.2 * lp : 0.5 - 0.47 * lp, drive = 1 + 5 * lp;
        const saw = (x) => 2 * (x - Math.floor(x)) - 1, tri = (x) => (x < 0.5 ? 4 * x - 1 : 3 - 4 * x);
        if (w === 0) s = saw(ph);
        else if (w === 1 || w === 2) s = ph < pw ? 1 : -1;
        else if (w === 3) s = tri(ph);
        else if (w === 4) s = Math.sin(ph * 2 * Math.PI);
        else s = 0.5 * (tri(ph) + saw(ph));
        if ((w === 0 || w >= 3) && lp > 0.001) s = Math.tanh(s * drive) / Math.tanh(drive);
      } else if (m === 2) {
        const I = lp * lp * 1.6, ra = [0.5, 1, 1.5, 2, 3, 4, 5, 6, 7, 8, 11, 13][VALUES[id(pre + "ratio")]];
        s = Math.sin(2 * Math.PI * (ph + I * Math.sin(2 * Math.PI * ra * ph)));
      } else {
        lpst += (r() * 2 - 1 - lpst) * (1 - lp * 0.97);
        s = clamp(lpst * (1 + lp * 2.5), -1, 1);
      }
      ys[i] = s;
    }
    g.strokeStyle = "#2a2826";
    g.beginPath(); g.moveTo(0, H / 2 + 0.5); g.lineTo(W, H / 2 + 0.5); g.stroke();
    g.strokeStyle = "#C63F3E";
    g.lineWidth = 2;
    g.lineJoin = "round";
    g.beginPath();
    for (let i = 0; i < N; i++) {
      const x = 16 + (i / (N - 1)) * (W - 32), y = H / 2 - ys[i] * H * 0.3;
      i ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
  }

  /* ------------------------------------------------------------------ the filter --- */
  function cmul(a, b) { return [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]]; }
  function cabs(a) { return Math.hypot(a[0], a[1]); }
  function fltMag(f, fc, res, type, slope, mode) {
    const w = f / fc, s = [0, w];
    if (type === 0) {                         /* ladder: k = 4.3 res^2, 4 poles or 2 */
      const k = res * res * 4.3, one = [1, w];
      if (slope === 1) {
        const q = cmul(cmul(one, one), cmul(one, one)), D = [q[0] + k, q[1]];
        const num = mode === 0 ? cabs([1 + 0.5 * k, 0]) : mode === 2 ? Math.pow(w, 4) * (1 + 0.15 * k) : 4 * w * w * (1 + 0.1 * k);
        return num / cabs(D);
      }
      const q = cmul(one, one), k2 = k * 1.6, D = [q[0] + k2 * 0.25, q[1] * (1 - 0.18 * res)];
      const num = mode === 0 ? 1 + 0.25 * k2 : mode === 2 ? w * w : 2 * w;
      return num / cabs(D);
    }
    const Q = 0.5 + 11 * res * res, D = [1 - w * w, w / Q];
    let H = mode === 0 ? 1 / cabs(D) : mode === 2 ? (w * w) / cabs(D) : w / Q / cabs(D);
    if (slope === 1) H = H * H * (mode === 1 ? 1 : 1);
    return H;
  }
  let fltCv;
  function drawFilter() {
    const g = prep(fltCv);
    if (!g) return;
    const W = fltCv.cw, H = fltCv.ch;
    grid(g, W, H);
    const fon = val("flt.on"), fc = val("flt.cutoff"), res = val("flt.res"), type = val("flt.type"), slope = val("flt.slope"), mode = val("flt.mode");
    const dbTop = 24, dbBot = -48, yOf = (db) => ((dbTop - clamp(db, dbBot, dbTop)) / (dbTop - dbBot)) * H;
    g.strokeStyle = "#2a2826";
    g.beginPath(); g.moveTo(0, yOf(0) + 0.5); g.lineTo(W, yOf(0) + 0.5); g.stroke();
    const pts = [];
    for (let x = 0; x <= W; x += 2) {
      const f = 20 * Math.pow(1000, x / W), m = fon ? fltMag(f, fc, res, type, slope, mode) : 1;
      pts.push([x, yOf(20 * Math.log10(Math.max(1e-6, m)))]);
    }
    g.beginPath();
    pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.lineTo(W, H); g.lineTo(0, H); g.closePath();
    g.fillStyle = fon ? "rgba(198,63,62,.12)" : "rgba(140,135,130,.06)";
    g.fill();
    g.beginPath();
    pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.strokeStyle = fon ? "#C63F3E" : "#5b5752";
    g.lineWidth = 2;
    g.stroke();
    const hx = (Math.log(fc / 20) / Math.log(1000)) * W;
    const hy = yOf(20 * Math.log10(Math.max(1e-6, fon ? fltMag(fc, fc, res, type, slope, mode) : 1)));
    g.fillStyle = "#F2E9D5";
    g.fillRect(hx - 4, clamp(hy, 4, H - 4) - 4, 8, 8);
    g.font = "10px 'DM Mono', monospace";
    g.fillStyle = "#8C8782";
    g.fillText(fmtHz(fc).toUpperCase(), 8, 14);
    g.fillText(["MOOG LADDER", "CLEAN SVF"][type] + " · " + ["12", "24"][slope] + " · " + ["LP", "BP", "HP"][mode], 8, H - 8);
  }
  function filterWire() {
    fltCv = $("#fltc");
    fixHeight(fltCv);
    ["flt.on", "flt.cutoff", "flt.res", "flt.type", "flt.slope", "flt.mode"].forEach((k) => on(id(k), () => later(drawFilter)));
    let idp = null;
    const at = (e) => {
      const r = fltCv.getBoundingClientRect();
      set(id("flt.cutoff"), 20 * Math.pow(1000, clamp((e.clientX - r.left) / r.width, 0, 1)));
      set(id("flt.res"), clamp(1 - (e.clientY - r.top) / r.height, 0, 1) * 1.15 - 0.08);
    };
    fltCv.style.cursor = "crosshair";
    fltCv.addEventListener("pointerdown", (e) => { idp = e.pointerId; fltCv.setPointerCapture(idp); at(e); });
    fltCv.addEventListener("pointermove", (e) => { if (e.pointerId === idp) at(e); });
    fltCv.addEventListener("pointerup", () => { idp = null; });
    const chips = $("#m-flt .chips");
    chips.innerHTML = `<span class="lbl">ENTRA</span>` +
      [["a", "A"], ["b", "B"], ["c", "C"], ["sub", "SUB"], ["noise", "NOISE"]].map(([k, l]) => `<button class="seg1" type="button" data-k="flt.route.${k}" aria-label="${l} passa pelo filtro">${l}</button>`).join("");
  }

  /* ------------------------------------------------------------------ envelopes, LFOs --- */
  const ENVS = [], LFOS = [];
  let envTab = 0, lfoTab = 0, fxTab = 0;
  function tabs(host, names, onpick) {
    host.setAttribute("role", "tablist");
    host.innerHTML = names.map((n, i) => `<button type="button" role="tab" aria-selected="${i === 0}">${n}</button>`).join("");
    const bs = $$("button", host);
    bs.forEach((b, i) => b.addEventListener("click", () => { bs.forEach((x, j) => x.setAttribute("aria-selected", i === j ? "true" : "false")); onpick(i); }));
    return bs;
  }
  function buildEnv() {
    const box = $("#m-env .envbox");
    for (let k = 1; k <= 3; k++) {
      const d = document.createElement("div");
      d.className = "page";
      d.hidden = k !== 1;
      d.innerHTML = `<div class="screen"><canvas height="104"></canvas><div class="tr lbl">${k === 1 ? "AMP" : "MOD"}</div></div>
        <div class="oscrow"><div class="knob" data-k="env${k}.a" data-label="ATTACK"></div><div class="knob" data-k="env${k}.d" data-label="DECAY"></div>
        <div class="knob" data-k="env${k}.s" data-label="SUSTAIN"></div><div class="knob" data-k="env${k}.r" data-label="RELEASE"></div></div>`;
      box.appendChild(d);
      const cv = $("canvas", d);
      fixHeight(cv);
      ENVS.push({ k, d, cv, hs: [] });
    }
    tabs($("#m-env [data-tabs=env]"), ["ENV 1", "ENV 2", "ENV 3"], (i) => {
      envTab = i;
      ENVS.forEach((e, j) => { e.d.hidden = i !== j; });
      drawEnv(ENVS[i]);
      dragLabels();
    });
  }
  function envGeom(e) {
    const W = e.cv.cw, H = e.cv.ch, k = e.k, pa = BYKEY[`env${k}.a`], pd = BYKEY[`env${k}.d`], pr = BYKEY[`env${k}.r`];
    const x0 = 12, w = W - 24, top = 12, bot = H - 12, seg = w * 0.28;
    const s = val(`env${k}.s`);
    const xa = x0 + Math.max(1, norm(pa, val(pa.key)) * seg), xd = xa + Math.max(1, norm(pd, val(pd.key)) * seg);
    const xs = xd + w * 0.16, xr = xs + Math.max(1, norm(pr, val(pr.key)) * seg);
    const ys = bot - s * (bot - top);
    return { W, H, x0, top, bot, seg, s, xa, xd, xs, xr, ys, pa, pd, pr };
  }
  function drawEnv(e) {
    const g = prep(e.cv);
    if (!g) return;
    const G = envGeom(e);
    grid(g, G.W, G.H);
    const pts = [[G.x0, G.bot], [G.xa, G.top]];
    for (let i = 1; i <= 24; i++) { const t = i / 24; pts.push([G.xa + (G.xd - G.xa) * t, G.ys - (G.ys - G.top) * Math.exp(-4.6 * t) + (G.ys - G.top) * Math.exp(-4.6) * t]); }
    pts.push([G.xs, G.ys]);
    for (let i = 1; i <= 24; i++) { const t = i / 24; pts.push([G.xs + (G.xr - G.xs) * t, G.bot - (G.bot - G.ys) * (Math.exp(-4.6 * t) - Math.exp(-4.6) * t)]); }
    g.beginPath();
    pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.lineTo(G.xr, G.bot);
    g.closePath();
    g.fillStyle = "rgba(198,63,62,.12)";
    g.fill();
    g.beginPath();
    pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.strokeStyle = "#C63F3E";
    g.lineWidth = 2;
    g.stroke();
    g.setLineDash([2, 3]);
    g.strokeStyle = "#3b3834";
    g.lineWidth = 1;
    g.beginPath();
    [G.xa, G.xd, G.xs].forEach((x) => { g.moveTo(Math.round(x) + 0.5, G.top); g.lineTo(Math.round(x) + 0.5, G.bot); });
    g.stroke();
    g.setLineDash([]);
    e.hs = [[G.xa, G.top, "a"], [G.xd, G.ys, "d"], [G.xr, G.bot, "r"]];
    g.fillStyle = "#F2E9D5";
    e.hs.forEach(([x, y]) => g.fillRect(x - 4, y - 4, 8, 8));
    const lv = UI ? UI[8 + e.k - 1] : 0;
    if (lv > 0.001) { g.fillStyle = "#C63F3E"; g.fillRect(3, G.bot - lv * (G.bot - G.top), 4, lv * (G.bot - G.top)); }
    e.lv = lv;
  }
  function envWire(e) {
    ["a", "d", "s", "r"].forEach((s) => on(id(`env${e.k}.${s}`), () => later(() => drawEnv(e))));
    let idp = null, h = null;
    e.cv.style.cursor = "pointer";
    e.cv.addEventListener("pointerdown", (ev) => {
      const r = e.cv.getBoundingClientRect(), x = ev.clientX - r.left, y = ev.clientY - r.top;
      let best = 1e9;
      h = null;
      e.hs.forEach((hh) => { const d = Math.hypot(hh[0] - x, hh[1] - y); if (d < best) { best = d; h = hh[2]; } });
      if (best > 40) { h = null; return; }
      idp = ev.pointerId;
      e.cv.setPointerCapture(idp);
    });
    e.cv.addEventListener("pointermove", (ev) => {
      if (ev.pointerId !== idp || !h) return;
      const r = e.cv.getBoundingClientRect(), x = ev.clientX - r.left, y = ev.clientY - r.top, G = envGeom(e);
      if (h === "a") set(G.pa.id, denorm(G.pa, (x - G.x0) / G.seg));
      if (h === "d") {
        set(G.pd.id, denorm(G.pd, (x - G.xa) / G.seg));
        set(id(`env${e.k}.s`), clamp((G.bot - y) / (G.bot - G.top), 0, 1));
      }
      if (h === "r") set(G.pr.id, denorm(G.pr, (x - G.xs) / G.seg));
    });
    e.cv.addEventListener("pointerup", () => { idp = null; h = null; });
  }
  function buildLfo() {
    const box = $("#m-lfo .lfobox");
    for (let k = 1; k <= 3; k++) {
      const d = document.createElement("div");
      d.className = "page";
      d.hidden = k !== 1;
      d.innerHTML = `<div class="screen"><canvas height="78"></canvas><div class="tr lbl lrate"></div></div>
        <div class="ctl"><div class="seg" data-k="lfo${k}.shape" data-icons="lfo"></div></div>
        <div class="oscrow"><div class="knob" data-k="lfo${k}.rate" data-label="RATE"></div>
          <label class="stack"><span class="lbl">SYNC</span><select class="sel" data-k="lfo${k}.sync"></select></label>
          <label class="stack"><span class="lbl">MODE</span><select class="sel" data-k="lfo${k}.mode"></select></label>
          <div class="knob" data-k="lfo${k}.smooth" data-size="S" data-label="SMOOTH"></div></div>`;
      box.appendChild(d);
      const cv = $("canvas", d);
      fixHeight(cv);
      LFOS.push({ k, d, cv });
    }
    tabs($("#m-lfo [data-tabs=lfo]"), ["LFO 1", "LFO 2", "LFO 3"], (i) => {
      lfoTab = i;
      LFOS.forEach((e, j) => { e.d.hidden = i !== j; });
      drawLfo(LFOS[i]);
      dragLabels();
    });
  }
  function lfoShape(shape, ph, seg, r) {
    switch (shape) {
      case 0: return Math.sin(ph * 2 * Math.PI);
      case 1: return ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph;
      case 2: return 2 * ph - 1;
      case 3: return 1 - 2 * ph;
      case 4: return ph < 0.5 ? 1 : -1;
      case 5: return r[seg % r.length];
      default: { const t = ph * ph * (3 - 2 * ph); return r[seg % r.length] * (1 - t) + r[(seg + 1) % r.length] * t; }
    }
  }
  function drawLfo(e) {
    const g = prep(e.cv);
    if (!g) return;
    const W = e.cv.cw, H = e.cv.ch, k = e.k, shape = val(`lfo${k}.shape`), sm = val(`lfo${k}.smooth`);
    grid(g, W, H);
    const rnd = [0.7, -0.4, 0.2, -0.9, 0.5, -0.1, 0.95, -0.6], N = 240, x0 = 10, w = W - 20, amp = H * 0.36, cyc = 2;
    let y = 0;
    const a = 1 - sm * 0.985, ys = [];
    for (let pass = 0; pass < 2; pass++)
      for (let i = 0; i < N; i++) {
        const t = (i / N) * cyc, seg = Math.floor(t), v = lfoShape(shape, t - seg, seg, rnd);
        y += (v - y) * (sm > 0.001 ? a : 1);
        if (pass) ys.push(y);
      }
    g.strokeStyle = "#2a2826";
    g.beginPath(); g.moveTo(0, H / 2 + 0.5); g.lineTo(W, H / 2 + 0.5); g.stroke();
    g.beginPath();
    ys.forEach((v, i) => { const x = x0 + (i / (N - 1)) * w, yy = H / 2 - v * amp; i ? g.lineTo(x, yy) : g.moveTo(x, yy); });
    g.strokeStyle = "#C63F3E";
    g.lineWidth = 2;
    g.stroke();
    if (UI) {
      const ph = UI[11 + k - 1], v = UI[5 + k - 1];
      g.fillStyle = "#F2E9D5";
      g.fillRect(x0 + (ph / cyc) * w - 3, H / 2 - v * amp - 3, 6, 6);
      e.ph = ph;
    }
    const sync = val(`lfo${k}.sync`);
    $(".lrate", e.d).textContent = sync ? BYKEY[`lfo${k}.sync`].opts[sync] : fmtHz(val(`lfo${k}.rate`)).toUpperCase();
  }
  function lfoWire(e) {
    const k = e.k, rk = KNOBS.get(`lfo${k}.rate`)[0];
    ["shape", "smooth", "rate", "sync"].forEach((s) => on(id(`lfo${k}.${s}`), () => later(() => drawLfo(e))));
    on(id(`lfo${k}.sync`), (v) => rk.el.classList.toggle("dim", !!v));
  }

  /* ------------------------------------------------------------------ FX, macros --- */
  const FXDEF = [
    { name: "DIST", on: "fx.dist.on", sel: ["fx.dist.type"], knobs: ["fx.dist.drive", "fx.dist.tone", "fx.dist.mix"] },
    { name: "CHORUS", on: "fx.cho.on", knobs: ["fx.cho.rate", "fx.cho.depth", "fx.cho.mix"] },
    { name: "DELAY", on: "fx.dly.on", sel: ["fx.dly.time"], tog: [["fx.dly.ping", "PING-PONG"]], knobs: ["fx.dly.fb", "fx.dly.tone", "fx.dly.mix"] },
    { name: "REVERB", on: "fx.rev.on", knobs: ["fx.rev.size", "fx.rev.damp", "fx.rev.mix"] },
    { name: "COMP", on: "fx.comp.on", knobs: ["fx.comp.amount", "fx.comp.mix"] },
  ];
  function fxControls(f, withLed, size) {
    return `<div class="ctl">${withLed ? `<button class="led" data-k="${f.on}" aria-label="${f.name} ligado"></button><span class="lbl">${f.name}</span>` : ""}
      ${(f.sel || []).map((k) => `<select class="sel" data-k="${k}"></select>`).join("")}
      ${(f.tog || []).map(([k, l]) => `<button class="seg1" type="button" data-k="${k}">${l}</button>`).join("")}</div>
      <div class="oscrow">${f.knobs.map((k) => `<div class="knob" data-k="${k}"${size ? ` data-size="${size}"` : ""}></div>`).join("")}</div>`;
  }
  function buildFx() {
    const box = $("#m-fx .fxbox");
    const panels = FXDEF.map((f, i) => {
      const d = document.createElement("div");
      d.className = "page";
      d.hidden = i !== 0;
      d.dataset.power = f.on;
      d.innerHTML = fxControls(f, true);
      box.appendChild(d);
      return d;
    });
    const bs = tabs($("#m-fx [data-tabs=fx]"), FXDEF.map((f) => f.name), (i) => { fxTab = i; panels.forEach((p, j) => { p.hidden = i !== j; }); });
    FXDEF.forEach((f, i) => { bs[i].dataset.lit = f.on; });
    const rack = $("#fxrack");
    rack.innerHTML = FXDEF.map((f) => `<section class="mod" data-power="${f.on}"><div class="mh"><h2>${f.name}</h2>
      <button class="led" data-k="${f.on}" aria-label="${f.name} ligado"></button></div><div class="body page">${fxControls(f, false, "L")}</div></section>`).join("");
  }
  function buildMacros() {
    $("#macros").innerHTML = [1, 2, 3, 4].map((k) => `<div class="mac"><div class="knob" data-k="macro${k}" data-label="MACRO ${k}"></div>
      <span class="drag" data-src="MACRO ${k}"></span></div>`).join("");
  }

  /* ------------------------------------------------------------------ the matrix --- */
  function buildMatrix() {
    const row = (m) => `<div class="mxr"><span class="i">${String(m).padStart(2, "0")}</span>
      <select class="sel" data-k="mod${m}.src" aria-label="Origem ${m}"></select><span class="ar">&#8594;</span>
      <select class="sel" data-k="mod${m}.dst" aria-label="Destino ${m}"></select>
      <div class="amt" data-k="mod${m}.amt"><i></i><span></span></div></div>`;
    let h = "";
    for (let m = 1; m <= 6; m++) h += row(m);
    $("#mxshort").innerHTML = h;
    h = "";
    for (let m = 1; m <= 16; m++) h += row(m);
    $("#mxfull").innerHTML = h;
  }
  function modRings() {
    const ranges = {};
    for (let m = 1; m <= 16; m++) {
      const s = VALUES[id(`mod${m}.src`)], d = VALUES[id(`mod${m}.dst`)], a = VALUES[id(`mod${m}.amt`)];
      if (!s || !d || !a) continue;
      const key = INFO.dests[d].key;
      if (!key || key === "pitch") continue;
      const bip = BIPOLAR.has(INFO.sources[s]);
      const r = ranges[key] || (ranges[key] = [0, 0]);
      if (bip) { r[0] -= Math.abs(a); r[1] += Math.abs(a); } else if (a > 0) r[1] += a; else r[0] += a;
    }
    for (const [key, ks] of KNOBS) {
      const r = ranges[key];
      for (const k of ks) {
        if (!k.setMod) continue;
        if (!r) k.setMod(null);
        else { const n = norm(k.p, VALUES[k.p.id]); k.setMod(n + r[0], n + r[1]); }
      }
    }
    OSCS.forEach((o) => { o.live = -1; });
  }
  function addRoute(srcName, destIdx) {
    const s = INFO.sources.indexOf(srcName);
    if (s < 1) return;
    let free = -1;
    for (let m = 1; m <= 16; m++) {
      const ms = VALUES[id(`mod${m}.src`)], md = VALUES[id(`mod${m}.dst`)];
      if (ms === s && md === destIdx) { toast(`${srcName} já vai para ${INFO.dests[destIdx].name}`); return; }
      if (free < 0 && (!ms || !md)) free = m;
    }
    if (free < 0) { toast("A matriz está cheia: libere uma das 16 ligações."); return; }
    set(id(`mod${free}.src`), s);
    set(id(`mod${free}.dst`), destIdx);
    set(id(`mod${free}.amt`), 0.5);
    toast(`${srcName} → ${INFO.dests[destIdx].name} · 50 · ajuste na MATRIX`);
  }
  /* drag a source onto a knob */
  function dragSource(el) {
    el.innerHTML = `<svg viewBox="0 0 12 12"><circle cx="6" cy="6" r="4.5"/><path d="M6 3.5v5M3.5 6h5"/></svg><span></span>`;
    let ghost = null, idp = null, over = null;
    el.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      idp = e.pointerId;
      el.setPointerCapture(idp);
      ghost = document.createElement("div");
      ghost.className = "ghost";
      ghost.textContent = srcOf(el);
      document.body.appendChild(ghost);
      move(e);
    });
    const target = (e) => {
      const t = document.elementFromPoint(e.clientX, e.clientY);
      const k = t && t.closest(".knob, .stepper");
      return k && DKEY[k.dataset.k] !== undefined ? k : null;
    };
    function move(e) {
      if (e.pointerId !== idp) return;
      ghost.style.left = e.clientX + 12 + "px";
      ghost.style.top = e.clientY + 10 + "px";
      const k = target(e);
      if (over !== k) { if (over) over.classList.remove("drop"); over = k; if (over) over.classList.add("drop"); }
    }
    el.addEventListener("pointermove", move);
    const end = (e) => {
      if (e.pointerId !== idp) return;
      idp = null;
      if (ghost) ghost.remove();
      ghost = null;
      if (over) { over.classList.remove("drop"); addRoute(srcOf(el), DKEY[over.dataset.k]); over = null; }
    };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);
  }
  function srcOf(el) {
    const s = el.dataset.src;
    if (s === "env") return "ENV " + (envTab + 1);
    if (s === "lfo") return "LFO " + (lfoTab + 1);
    if (s === "seq") { const l = el.closest(".mod").querySelector(".sequi").lane || 4; return "SEQ MOD " + (l >= 4 ? l - 3 : 1); }
    return s;
  }
  function dragLabels() { $$(".drag[data-src]").forEach((el) => { $("span", el).textContent = srcOf(el); el.title = "Arraste até um knob para modular"; }); }

  /* ------------------------------------------------------------------ the sequencer --- */
  const SEQS = [];
  let wantPlay = false;
  function buildSeq(host) {
    host.innerHTML = `<div class="seqctl"><div class="mtabs lanes"></div><span class="sp"></span>
      <label class="stack"><span class="lbl">RATE</span><select class="sel" data-k="seq.rate"></select></label>
      <label class="stack"><span class="lbl">LENGTH</span><select class="sel" data-k="seq.len"></select></label>
      <label class="stack"><span class="lbl">DIRECTION</span><select class="sel" data-k="seq.dir"></select></label>
      <div class="stepper" data-k="seq.oct" data-label="OCT"></div>
      <div class="knob" data-k="seq.swing" data-size="S" data-label="SWING"></div></div>
      <div class="seqbox"><button class="play" type="button" aria-pressed="false" aria-label="Tocar o sequencer"><svg viewBox="0 0 14 14"><path d="M2 1l11 6-11 6z"/></svg></button>
      <div class="steps" role="grid" aria-label="Passos"></div></div>`;
    const steps = $(".steps", host);
    steps.innerHTML = Array.from({ length: 16 }, (_, i) => `<div class="stp" data-i="${i}"><i></i><b></b></div>`).join("");
    host.lane = 0;
    const st = { host, steps, cells: $$(".stp", steps), play: $(".play", host), last: -2 };
    tabs($(".lanes", host), LANE_LABEL, (i) => { host.lane = i; renderSeq(st); dragLabels(); });
    st.play.addEventListener("click", () => transport(!wantPlay));
    let idp = null, moved = false, downStep = -1, y0 = 0;
    const cellAt = (e) => {
      const r = steps.getBoundingClientRect(), i = clamp(Math.floor(((e.clientX - r.left - 4) / (r.width - 8)) * 16), 0, 15);
      const v = clamp(1 - (e.clientY - r.top - 4) / (r.height - 8), 0, 1);
      return [i, v];
    };
    const paint = (e) => {
      const [i, v] = cellAt(e), lane = host.lane;
      let x = v;
      if (lane === 1) x = Math.round(v * 24 - 12);
      else if (lane === 0 && v < 0.06) x = 0;
      set(seqId(i, lane), x);
    };
    steps.addEventListener("pointerdown", (e) => {
      idp = e.pointerId;
      steps.setPointerCapture(idp);
      moved = false;
      y0 = e.clientY;
      downStep = cellAt(e)[0];
      if (host.lane !== 0) paint(e);
    });
    steps.addEventListener("pointermove", (e) => {
      if (e.pointerId !== idp) return;
      if (!moved && (Math.abs(e.clientY - y0) > 3 || cellAt(e)[0] !== downStep)) moved = true;
      if (moved) paint(e);
    });
    steps.addEventListener("pointerup", (e) => {
      if (e.pointerId !== idp) return;
      idp = null;
      if (host.lane === 0 && !moved) { const g = seqId(downStep, 0); set(g, VALUES[g] > 0 ? 0 : 0.6); }
    });
    SEQS.push(st);
    return st;
  }
  function renderSeq(st) {
    const lane = st.host.lane, len = val("seq.len");
    st.cells.forEach((c, i) => {
      const v = VALUES[seqId(i, lane)], bar = $("i", c), b = $("b", c);
      c.classList.toggle("bip", lane === 1);
      c.classList.toggle("zero", lane === 1);
      c.classList.toggle("out", i >= len);
      if (lane === 1) {
        const h = (clamp(v, -12, 12) / 12) * 50;
        bar.style.bottom = h >= 0 ? "50%" : 50 + h + "%";
        bar.style.top = "auto";
        bar.style.height = Math.max(1.5, Math.abs(h)) + "%";
        b.textContent = v ? (v > 0 ? "+" : "") + v : "";
        bar.style.opacity = VALUES[seqId(i, 0)] > 0 ? "" : ".35";
      } else {
        bar.style.top = "";
        bar.style.bottom = "0";
        bar.style.height = v * 100 + "%";
        b.textContent = "";
        bar.style.opacity = lane === 0 || VALUES[seqId(i, 0)] > 0 ? "" : ".35";
      }
    });
  }
  function transport(play) {
    wantPlay = play;
    startAudio().then(() => post({ t: "play", v: play ? 1 : 0 }), () => {});
    SEQS.forEach((s) => s.play.setAttribute("aria-pressed", play ? "true" : "false"));
  }

  /* ------------------------------------------------------------------ the keyboard --- */
  const KEYMAP = { KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7, KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14, KeyP: 15, Semicolon: 16 };
  const KEYLBL = ["A", "W", "S", "E", "D", "F", "T", "G", "Y", "H", "U", "J", "K", "O", "L", "P", "Ç"];
  let kbBase = 60, sustainOn = false;
  const LO = 36, HI = 84, keyEl = {};
  function buildKeys() {
    const host = $("#keys");
    const whites = [];
    for (let n = LO; n <= HI; n++) if (![1, 3, 6, 8, 10].includes(n % 12)) whites.push(n);
    const ww = 100 / whites.length;
    let h = "";
    whites.forEach((n) => { h += `<div class="wk" data-n="${n}">${n % 12 === 0 ? noteName(n) : ""}</div>`; });
    for (let n = LO; n <= HI; n++) {
      if (![1, 3, 6, 8, 10].includes(n % 12)) continue;
      const wi = whites.indexOf(n - 1);
      h += `<div class="bk" data-n="${n}" style="left:calc(${(wi + 1) * ww}% - ${ww * 0.32}%);width:${ww * 0.62}%"></div>`;
    }
    host.innerHTML = h;
    $$("[data-n]", host).forEach((k) => { keyEl[+k.dataset.n] = k; });
    const ptr = new Map();
    const noteAt = (e) => { const t = document.elementFromPoint(e.clientX, e.clientY); const k = t && t.closest("[data-n]"); return k && host.contains(k) ? k : null; };
    const velAt = (k, e) => { const r = k.getBoundingClientRect(); return Math.round(clamp(30 + ((e.clientY - r.top) / r.height) * 100, 20, 127)); };
    host.addEventListener("pointerdown", (e) => {
      const k = noteAt(e);
      if (!k) return;
      e.preventDefault();
      host.setPointerCapture(e.pointerId);
      const n = +k.dataset.n;
      ptr.set(e.pointerId, n);
      noteOn(n, velAt(k, e));
    });
    host.addEventListener("pointermove", (e) => {
      if (!ptr.has(e.pointerId)) return;
      const k = noteAt(e);
      const n = k ? +k.dataset.n : null, old = ptr.get(e.pointerId);
      if (n !== null && n !== old) { noteOff(old); ptr.set(e.pointerId, n); noteOn(n, velAt(k, e)); }
    });
    const up = (e) => { if (!ptr.has(e.pointerId)) return; noteOff(ptr.get(e.pointerId)); ptr.delete(e.pointerId); };
    host.addEventListener("pointerup", up);
    host.addEventListener("pointercancel", up);
    keyLabels();
    requestAnimationFrame(() => { const sc = $(".kscroll"), k = keyEl[kbBase - 5]; if (sc && k && sc.scrollWidth > sc.clientWidth) sc.scrollLeft = k.offsetLeft; });
  }
  function keyLabels() {
    for (let n = LO; n <= HI; n++) {
      const k = keyEl[n];
      if (!k) continue;
      const i = n - kbBase;
      const lbl = i >= 0 && i < KEYLBL.length ? KEYLBL[i] : "";
      k.textContent = lbl || (n % 12 === 0 ? noteName(n) : "");
    }
    $("#octname").textContent = noteName(kbBase);
  }
  function noteOn(n, v) {
    if (n < 0 || n > 127) return;
    held.set(n, v);
    if (keyEl[n]) keyEl[n].classList.add("down");
    if (node) { post({ t: "on", n, v }); if (ctx.state !== "running") ctx.resume().then(powerLabel); }
    else startAudio();
  }
  function noteOff(n) {
    if (!held.has(n)) return;
    held.delete(n);
    if (keyEl[n]) keyEl[n].classList.remove("down");
    post({ t: "off", n });
  }
  function typing(e) { const t = e.target; return t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable); }
  function keyWire() {
    const down = new Set(), downNote = {};
    window.addEventListener("keydown", (e) => {
      if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
      if (e.code === "Space") {                /* SPACE plays the sequencer, wherever the focus is */
        if (e.target && e.target.tagName === "SELECT") return;
        e.preventDefault();
        if (!e.repeat) transport(!wantPlay);
        return;
      }
      if (e.code === "KeyZ" || e.code === "KeyX") { if (!e.repeat) { kbBase = clamp(kbBase + (e.code === "KeyZ" ? -12 : 12), 24, 96); keyLabels(); } return; }
      if (!(e.code in KEYMAP)) return;
      e.preventDefault();
      if (e.repeat || down.has(e.code)) return;
      const n = kbBase + KEYMAP[e.code];
      down.add(e.code);
      downNote[e.code] = n;
      noteOn(n, 100);
    });
    window.addEventListener("keyup", (e) => {
      if (e.code === "Space" && !typing(e) && !(e.target && e.target.tagName === "SELECT")) e.preventDefault();
      if (!down.has(e.code)) return;
      down.delete(e.code);
      noteOff(downNote[e.code]);
    });
    window.addEventListener("blur", () => { for (const c of down) noteOff(downNote[c]); down.clear(); });
    $("#octdn").addEventListener("click", () => { kbBase = clamp(kbBase - 12, 24, 96); keyLabels(); });
    $("#octup").addEventListener("click", () => { kbBase = clamp(kbBase + 12, 24, 96); keyLabels(); });
    $("#holdk").addEventListener("click", (e) => {
      sustainOn = !sustainOn;
      e.currentTarget.setAttribute("aria-pressed", sustainOn ? "true" : "false");
      startAudio().then(() => post({ t: "cc", c: 64, v: sustainOn ? 127 : 0 }), () => {});
    });
    $("#panic").addEventListener("click", () => {
      transport(false);
      for (const n of Array.from(held.keys())) noteOff(n);
      post({ t: "panic" });
      if (sustainOn) $("#holdk").click();
    });
  }
  function midiWire() {
    const st = $("#midistate");
    if (!navigator.requestMIDIAccess) { st.textContent = "Este navegador não tem Web MIDI (Chrome e Edge têm)."; return; }
    let req;
    try { req = navigator.requestMIDIAccess({ sysex: false }); } catch (e) { st.textContent = "O navegador não liberou o MIDI nesta página."; return; }
    req.then((acc) => {
      const hook = () => {
        const names = [];
        acc.inputs.forEach((inp) => { inp.onmidimessage = midi; names.push(inp.name); });
        st.textContent = names.length ? "Conectado: " + names.join(", ") : "Nenhum controlador MIDI conectado.";
      };
      acc.onstatechange = hook;
      hook();
    }, () => { st.textContent = "O navegador não liberou o MIDI nesta página."; });
  }
  function midi(e) {
    const [s, a, b] = e.data, t = s & 0xf0;
    if (t === 0x90 && b > 0) noteOn(a, b);
    else if (t === 0x80 || (t === 0x90 && b === 0)) noteOff(a);
    else if (t === 0xb0) {
      if (a === 74) set(id("flt.cutoff"), denorm(BYKEY["flt.cutoff"], b / 127));
      else post({ t: "cc", c: a, v: b });
    } else if (t === 0xe0) post({ t: "bend", v: (((b << 7) | a) - 8192) / 8192 });
    else if (t === 0xd0) post({ t: "at", v: a / 127 });
  }

  /* ------------------------------------------------------------------ presets --- */
  let USER = [], FAV = new Set(), cur = { i: -1, n: "INIT", b: "A" }, dirty = false, saveT = 0;
  function loadMem() {
    try { USER = JSON.parse(mem.get("selva1.user") || "[]") || []; } catch (e) { USER = []; }
    try { FAV = new Set(JSON.parse(mem.get("selva1.fav") || "[]") || []); } catch (e) { FAV = new Set(); }
  }
  const all = () => PRESETS.concat(USER.map((u) => Object.assign({}, u, { b: "U" })));
  const favKey = (pr) => pr.b + ":" + pr.n;
  function patchNow() {
    const p = {}, s = {};
    for (let i = 0; i < SEQ0; i++) if (Math.abs(VALUES[i] - DEFS[i]) > 1e-7) p[P[i].key] = +VALUES[i].toPrecision(6);
    for (let l = 0; l < 8; l++) {
      const arr = [];
      let diff = false;
      for (let i = 0; i < 16; i++) { const v = VALUES[seqId(i, l)]; arr.push(+v.toPrecision(4)); if (v !== LANE_DEF[l]) diff = true; }
      if (diff) s[LANES[l]] = arr;
    }
    return { n: cur.n, p, s };
  }
  function applyPatch(pt) {
    const target = DEFS.slice();
    for (const k in pt.p || {}) if (BYKEY[k]) target[BYKEY[k].id] = pt.p[k];
    (pt.m || []).forEach(([s, d, a], i) => {
      if (i >= 16) return;
      target[id(`mod${i + 1}.src`)] = Math.max(0, INFO.sources.indexOf(s));
      target[id(`mod${i + 1}.dst`)] = Math.max(0, INFO.dests.findIndex((x) => x.name === d));
      target[id(`mod${i + 1}.amt`)] = a;
    });
    for (const ln in pt.s || {}) {
      const l = LANES.indexOf(ln);
      if (l < 0) continue;
      for (let i = 0; i < 16; i++) if (pt.s[ln][i] !== undefined) target[seqId(i, l)] = pt.s[ln][i];
    }
    loading = true;
    for (let i = 0; i < NALL; i++) set(i, target[i]);
    loading = false;
    later(modRings);
    SEQS.forEach(renderSeq);
  }
  function choose(i) {
    const L = all();
    if (!L.length) return;
    i = (i + L.length) % L.length;
    const pr = L[i];
    applyPatch(pr);
    cur = { i, n: pr.n, b: pr.b };
    dirty = false;
    showPreset();
    remember();
  }
  function initPatch() {
    applyPatch({ p: {} });
    cur = { i: -1, n: "INIT", b: cur.b };
    dirty = false;
    showPreset();
    remember();
  }
  function showPreset() {
    $("#pnum").textContent = cur.i < 0 ? "000" : pad3(cur.i + 1);
    $("#pnm").textContent = cur.n + (dirty ? " *" : "");
    $("#pbank").textContent = `BANK ${cur.b} / ${BANKS[cur.b] || ""}`;
    const fav = FAV.has(cur.b + ":" + cur.n);
    $("#pfav").setAttribute("aria-pressed", fav ? "true" : "false");
    const L = all(), sel = $("#psel");
    const groups = {};
    L.forEach((pr, i) => { (groups[pr.b] = groups[pr.b] || []).push(`<option value="${i}">${pad3(i + 1)} ${pr.n}${FAV.has(favKey(pr)) ? " ♥" : ""}</option>`); });
    sel.innerHTML = `<option value="-1">000 INIT</option>` + Object.keys(groups).map((b) => `<optgroup label="BANK ${b} / ${BANKS[b] || ""}">${groups[b].join("")}</optgroup>`).join("");
    sel.value = String(cur.i);
  }
  function touched() {
    if (!dirty) { dirty = true; $("#pnm").textContent = cur.n + " *"; }
    clearTimeout(saveT);
    saveT = setTimeout(remember, 500);
  }
  function remember() { mem.set("selva1.last", JSON.stringify({ cur, dirty, patch: patchNow() })); }
  function presetWire() {
    $("#pprev").addEventListener("click", () => choose(cur.i < 0 ? all().length - 1 : cur.i - 1));
    $("#pnext").addEventListener("click", () => choose(cur.i + 1));
    $("#psel").addEventListener("change", (e) => { const i = +e.target.value; i < 0 ? initPatch() : choose(i); });
    $("#pinit").addEventListener("click", initPatch);
    $("#pfav").addEventListener("click", () => {
      const k = cur.b + ":" + cur.n;
      FAV.has(k) ? FAV.delete(k) : FAV.add(k);
      mem.set("selva1.fav", JSON.stringify(Array.from(FAV)));
      showPreset();
    });
    const form = $("#saveform"), name = $("#savename");
    const openSave = () => {
      $("#moreform").hidden = true;
      form.hidden = false;
      name.value = cur.b === "U" ? cur.n : cur.n === "INIT" ? "MEU SOM" : cur.n + " 2";
      name.focus();
      name.select();
    };
    $("#psave").addEventListener("click", () => {
      if (cur.b !== "U") { openSave(); return; }
      const ui = USER.findIndex((u) => u.n === cur.n);
      if (ui < 0) { openSave(); return; }
      USER[ui] = Object.assign(patchNow(), { n: cur.n });
      mem.set("selva1.user", JSON.stringify(USER));
      dirty = false;
      showPreset();
      toast("Guardado: " + cur.n);
    });
    $("#psaveas").addEventListener("click", openSave);
    $("#savecancel").addEventListener("click", () => { form.hidden = true; });
    form.addEventListener("submit", (e) => {
      e.preventDefault();
      const n = name.value.trim().toUpperCase().slice(0, 22);
      if (!n) return;
      const pt = Object.assign(patchNow(), { n });
      const ui = USER.findIndex((u) => u.n === n);
      if (ui >= 0) USER[ui] = pt; else USER.push(pt);
      mem.set("selva1.user", JSON.stringify(USER));
      cur = { i: PRESETS.length + (ui >= 0 ? ui : USER.length - 1), n, b: "U" };
      dirty = false;
      form.hidden = true;
      showPreset();
      remember();
      toast("Guardado em BANK U: " + n);
    });
    $("#pmore").addEventListener("click", () => { form.hidden = true; const m = $("#moreform"); m.hidden = !m.hidden; });
    $("#pcopy").addEventListener("click", () => {
      const code = "SELVA1:" + btoa(unescape(encodeURIComponent(JSON.stringify(patchNow()))));
      const inp = $("#ppaste");
      const fallback = () => { inp.value = code; inp.focus(); inp.select(); toast("Patch no campo ao lado: copie com Ctrl+C."); };
      if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(code).then(() => toast("Patch copiado. Mande para quem quiser."), fallback);
      else fallback();
    });
    const paste = () => {
      const t = $("#ppaste").value.trim();
      if (!t.startsWith("SELVA1:")) return;
      try {
        const pt = JSON.parse(decodeURIComponent(escape(atob(t.slice(7)))));
        applyPatch(pt);
        cur = { i: -1, n: String(pt.n || "COLADO").toUpperCase().slice(0, 22), b: "U" };
        dirty = true;
        showPreset();
        remember();
        toast("Patch colado: " + cur.n);
      } catch (e) { toast("Esse texto não é um patch do Selva1."); }
    };
    $("#ppaste").addEventListener("change", paste);
    $("#ppaste").addEventListener("paste", () => setTimeout(paste, 0));
  }

  /* ------------------------------------------------------------------ pages, loop --- */
  function pageWire() {
    const bs = $$(".tabs [data-page]");
    const show = (pg) => {
      bs.forEach((b) => b.setAttribute("aria-selected", b.dataset.page === pg ? "true" : "false"));
      $$("section.page[data-page]").forEach((s) => { s.hidden = s.dataset.page !== pg; });
      redrawAll();
    };
    bs.forEach((b) => b.addEventListener("click", () => show(b.dataset.page)));
    $$("[data-goto]").forEach((b) => b.addEventListener("click", () => show(b.dataset.goto)));
    $("#power").addEventListener("click", () => {
      if (ctx && node && ctx.state === "running") ctx.suspend().then(powerLabel);
      else startAudio();
    });
    let rt = 0;
    window.addEventListener("resize", () => { clearTimeout(rt); rt = setTimeout(redrawAll, 80); });
  }
  function redrawAll() {
    OSCS.forEach((o) => { o.live = -1; drawOsc(o); });
    drawFilter();
    ENVS.forEach(drawEnv);
    LFOS.forEach(drawLfo);
    SEQS.forEach(renderSeq);
  }
  const ml = () => $("#ml"), mr = () => $("#mr");
  let lastStep = -1, lastPlaying = -1;
  function loop() {
    requestAnimationFrame(loop);
    if (UI) {
      const db = (v) => clamp((20 * Math.log10(Math.max(1e-5, v)) + 48) / 48, 0, 1) * 100 + "%";
      ml().style.height = db(UI[2]);
      mr().style.height = db(UI[3]);
      const step = UI[1] ? UI[0] : -1;
      if (step !== lastStep) {
        SEQS.forEach((s) => s.cells.forEach((c, i) => c.classList.toggle("now", i === step)));
        lastStep = step;
      }
      if (UI[1] !== lastPlaying) {
        lastPlaying = UI[1];
        SEQS.forEach((s) => s.play.setAttribute("aria-pressed", UI[1] ? "true" : "false"));
      }
      const e = ENVS[envTab];
      if (!e.d.hidden && Math.abs((e.lv || 0) - UI[8 + envTab]) > 0.004) drawEnv(e);
      const l = LFOS[lfoTab];
      if (l.ph !== UI[11 + lfoTab]) drawLfo(l);
    }
    const src = liveSources();
    OSCS.forEach((o) => {
      if (VALUES[id(o.pre + "mode")] === 3 || o.sec.closest("[hidden]")) return;
      const lp = liveNorm(o.pre + "pos", src);
      if (Math.abs(lp - o.live) > 0.003) { o.live = lp; drawOsc(o, lp); }
    });
  }

  /* ------------------------------------------------------------------ boot --- */
  function setup() {
    P = INFO.params;
    SEQ0 = INFO.seqBase;
    NALL = SEQ0 + INFO.steps * INFO.lanes;
    NSRC = INFO.sources.length;
    INFO.frames = X.s1_wt_frames();
    P.forEach((p) => { BYKEY[p.key] = p; });
    DEFS = new Float64Array(NALL);
    P.forEach((p) => { DEFS[p.id] = p.def; });
    for (let s = 0; s < 16; s++) for (let l = 0; l < 8; l++) DEFS[seqId(s, l)] = LANE_DEF[l];
    VALUES = DEFS.slice();
    INFO.dests.forEach((d, i) => { if (d.key && d.key !== "pitch") DKEY[d.key] = i; });
    ["a", "b", "c"].forEach((o) => { DKEY[`osc.${o}.semi`] = INFO.dests.findIndex((d) => d.name === `OSC ${o.toUpperCase()} PITCH`); });
    DKEY["global.tune"] = INFO.dests.findIndex((d) => d.name === "PITCH ALL");
    sharedDefs();
    TEX = [];
    for (let t = 0; t < 8; t++) TEX.push(knobTexture(t * 7 + 3));
    $$(".osc").forEach(buildOsc);
    filterWire();
    buildEnv();
    buildLfo();
    buildFx();
    buildMacros();
    buildMatrix();
    $$(".sequi").forEach(buildSeq);
    buildKeys();
    bind(document);
    OSCS.forEach(oscWire);
    ENVS.forEach(envWire);
    LFOS.forEach(lfoWire);
    $$(".mtabs [data-lit]").forEach((b) => on(id(b.dataset.lit), (v) => b.classList.toggle("lit", !!v)));
    on(id("seq.len"), () => SEQS.forEach(renderSeq));
    for (let s = 0; s < 16; s++) for (let l = 0; l < 8; l++) on(seqId(s, l), () => later(() => SEQS.forEach(renderSeq)));
    $$(".drag[data-src]").forEach(dragSource);
    dragLabels();
    keyWire();
    midiWire();
    pageWire();
    presetWire();
    loadMem();
    let last = null;
    try { last = JSON.parse(mem.get("selva1.last") || "null"); } catch (e) { last = null; }
    if (last && last.patch) {
      applyPatch(last.patch);
      cur = last.cur || cur;
      if (cur.i >= all().length) cur.i = -1;
      dirty = !!last.dirty;
      showPreset();
    } else choose(0);
    modRings();
    redrawAll();
    requestAnimationFrame(loop);
    document.body.classList.add("ready");
  }
  WebAssembly.instantiate(WASM, {}).then(({ instance }) => {
    X = instance.exports;
    X.s1_init(48000);
    INFO = JSON.parse(cstr(X.s1_info()));
    setup();
  }).catch((e) => {
    console.error(e);
    toast("O motor não carregou: " + (e && e.message ? e.message : e));
  });
  window.SELVA1 = { get values() { return VALUES; }, set: (k, v) => set(typeof k === "string" ? id(k) : k, v), get ui() { return UI; }, noteOn, noteOff, patchNow, applyPatch };
})();
