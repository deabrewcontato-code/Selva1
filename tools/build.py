#!/usr/bin/env python3
"""Selva1: builds the web instrument.
  dist/artifact.html      one self-contained page (engine inlined), for the Claude artifact
  dist/selva1/index.html  the same page as a full document, for a web host (deabrewmusic.com.br/selva1/)
  dist/selva1/selva1-worklet.js   the audio thread as a file (the page falls back to the inlined copy)
Run tools/build_wasm.sh first. Copyright (C) 2026 DeAbrew."""
import base64, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def rd(p):
    with open(os.path.join(ROOT, p), encoding="utf-8") as f:
        return f.read()

page = rd("web/index.html")
app = rd("web/app.js")
presets = rd("web/presets.js")
worklet = rd("web/worklet.js")
wasm = open(os.path.join(ROOT, "build/selva1.wasm"), "rb").read()
b64 = base64.b64encode(wasm).decode()

for tag in ("/*@PRESETS@*/", "/*@WORKLET@*/", '"@WASM_B64@"', "/*@APP@*/"):
    if page.count(tag) != 1:
        sys.exit("build: %s missing or repeated in web/index.html" % tag)
page = page.replace("/*@PRESETS@*/", presets)
page = page.replace("/*@WORKLET@*/", "const WORKLET_SRC = %s;" % json.dumps(worklet))
page = page.replace('"@WASM_B64@"', '"%s"' % b64)
page = page.replace("/*@APP@*/", app)
if "</script" in presets + worklet + app:
    sys.exit("build: a </script in the inlined code would end the page's script")

os.makedirs(os.path.join(ROOT, "dist/selva1"), exist_ok=True)
with open(os.path.join(ROOT, "dist/artifact.html"), "w", encoding="utf-8") as f:
    f.write(page)
# a full document: head = everything up to the first <div class="plate">, body = the rest
cut = page.index('<div class="plate"')
head, body = page[:cut], page[cut:]
full = ('<!doctype html>\n<html lang="pt-BR">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        '<meta name="theme-color" content="#141414">\n' + head + '</head>\n<body>\n' + body + '\n</body>\n</html>\n')
with open(os.path.join(ROOT, "dist/selva1/index.html"), "w", encoding="utf-8") as f:
    f.write(full)
with open(os.path.join(ROOT, "dist/selva1/selva1-worklet.js"), "w", encoding="utf-8") as f:
    f.write(worklet)
with open(os.path.join(ROOT, "dist/selva1-worklet.js"), "w", encoding="utf-8") as f:
    f.write(worklet)
print("dist/artifact.html %d bytes, dist/selva1/index.html %d bytes, wasm %d bytes" % (len(page.encode()), len(full.encode()), len(wasm)))
