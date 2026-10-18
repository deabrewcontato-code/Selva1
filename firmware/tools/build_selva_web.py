#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Selva1 web panel as one HTML file: web/selva/panel.html with the firmware's DSP (WebAssembly, the same C as the
FM-1's firmware) and the parameter ids / tables of its headers put inside.

  python3 tools/build_preview.py        (build/preview/sloop-preview.wasm)
  python3 tools/build_selva_web.py        (-> build/selva-web/index.html)

Needs a host C compiler (tools/selva_web_ids.c). No JieLi toolchain, no FM-1."""
import base64
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/selva-web"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = ROOT / "build/host/selva_web_ids"
    exe.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cc", "-O0", "-w", "-I" + str(ROOT / "build/gen"), "-o", str(exe), str(ROOT / "tools/selva_web_ids.c")],
                   check=True)
    ids = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    ids = ids.replace("export const ", "const ")
    wasm = (ROOT / "build/preview/sloop-preview.wasm").read_bytes()
    page = (ROOT / "web/selva/panel.html").read_text()
    page = page.replace("/*@IDS@*/", ids)
    page = page.replace("@WASM_B64@", base64.b64encode(wasm).decode())
    worklet = (ROOT / "web/selva/worklet.js").read_text()
    page = page.replace("/*@WORKLET@*/", "const WORKLET_SRC = " + repr_js(worklet) + ";")
    (OUT / "index.html").write_text(page)
    (OUT / "selva-worklet.js").write_text(worklet)            # next to the page (an artifact's file, a site's folder)
    print(f"Selva1 web panel: {OUT / 'index.html'} ({len(page) / 1024:.0f} KiB, the DSP {len(wasm) / 1024:.0f} KiB)")


def repr_js(s):
    import json
    return json.dumps(s)


if __name__ == "__main__":
    main()
