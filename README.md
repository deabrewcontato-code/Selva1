# Selva1

Sintetizador da DeAbrew inspirado na selva de pedra de São Paulo. Um motor em C portátil, sem bibliotecas,
que roda no navegador (WebAssembly) e vai virar plugin de DAW e uma versão leve para o M-VAVE FM-1.

**Tocar agora:** https://deabrewmusic.com.br/selva1/

## O som

- 3 osciladores (A, B, C), cada um WAVETABLE, ANALOG, FM ou NOISE, com até 8 vozes de uníssono, DETUNE, WIDTH, PAN.
- 14 wavetables próprias com nomes da cidade (SÃO PAULO, VIADUTO, CONCRETO, METRÔ, GAROA, NEON, AÇO, ASFALTO,
  SIRENE, BUZINA, MINHOCÃO, PAULISTA, TIETÊ, BÁSICA), sem aliasing (mipmaps por FFT), normalizadas por volume.
- SUB (seno ou quadrada, -1 ou -2 oitavas) e NOISE (branco, rosa, marrom, industrial, digital, vinil).
- Filtro MOOG LADDER (ZDF, afinado, auto-oscila) ou CLEAN SVF, 12/24 dB, LP/BP/HP, DRIVE, KEY, roteamento por fonte.
- 3 envelopes (ENV 1 é o volume), 3 LFOs (7 formas, sincronia com o BPM, FREE/RETRIG/ONE SHOT, SMOOTH).
- Matriz de modulação com 16 ligações, 20 origens, 30 destinos; 4 macros.
- FX: DIST (TAPE, TUBE, HARD, FOLD, CRUSH), CHORUS, DELAY (ping-pong, sincronizado), REVERB (FDN), COMP, limitador.
- SEQUENCER de 16 passos com 8 trilhas (GATE, NOTE, VELO, PROB, MOD 1-4) e ARP.
- 22 sons de fábrica em 4 bancos (A CONCRETE, B ASFALTO, C GAROA, D USINA).

## Estrutura

```
core/s1_math.h     matemática rápida sem libc
core/s1_params.h   a lista de parâmetros (uma linha por parâmetro: motor, painel e presets leem daqui)
core/s1_wt.c       as wavetables
core/selva1.c      o motor
tests/s1_test.c    testes no computador (extremos, afinação do filtro, sequencer, custo)
web/               o painel: index.html, app.js, worklet.js, presets.js
tools/build_wasm.sh   motor -> build/selva1.wasm
tools/build.py        painel -> dist/artifact.html e dist/selva1/ (para o site)
tools/render_presets.mjs  renderiza os sons de fábrica e mede o volume
```

## Compilar

```
sh tools/build_wasm.sh
cc -O2 -Icore -o build/s1_test tests/s1_test.c -lm && build/s1_test
python3 tools/build.py
```

Para publicar no site, envie `dist/selva1/index.html` e `dist/selva1/selva1-worklet.js` para a pasta `selva1`.

Copyright (C) 2026 DeAbrew.
