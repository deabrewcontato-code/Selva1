#!/bin/sh
# Selva1: the engine (core/selva1.c) as WebAssembly, freestanding (no libc, no imports).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
clang --target=wasm32 -O3 -ffreestanding -nostdlib -fno-builtin -Wno-unused-function -Icore -c core/selva1.c -o build/selva1.o
EXPORTS=""
for f in s1_init s1_set s1_get s1_nparams s1_nall s1_note_on s1_note_off s1_cc s1_bend s1_aftertouch s1_panic \
         s1_transport s1_render_io s1_buf_l s1_buf_r s1_buf_tmp s1_ui_state s1_wt_frame s1_wt_count s1_wt_frames s1_info; do
    EXPORTS="$EXPORTS --export=$f"
done
wasm-ld build/selva1.o -o build/selva1.wasm --no-entry --export-memory --initial-memory=33554432 -z stack-size=262144 $EXPORTS
echo "build/selva1.wasm: $(wc -c < build/selva1.wasm) bytes"
