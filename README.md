# Selva1

Sintetizador da DeAbrew inspirado na selva de pedra de São Paulo. Um motor em C portátil, sem bibliotecas,
que roda em três lugares:

| Onde | Como | Estado |
| --- | --- | --- |
| **Navegador** | WebAssembly + AudioWorklet | no ar: **https://deabrewmusic.com.br/selva1/** |
| **DAW** (Mac: AU e VST3; Windows: VST3) | plugin JUCE 8 com o mesmo painel numa web view | compilado pelo GitHub (Actions) |
| **M-VAVE FM-1** | firmware baseado no SLOOP, com o motor SELVA1 | compilado pelo GitHub (Actions) |

## O som

- 3 osciladores (A, B, C), cada um WAVETABLE, ANALOG, FM ou NOISE, com até 8 vozes de uníssono, DETUNE, WIDTH, PAN.
- 14 wavetables com nomes da cidade (SÃO PAULO, VIADUTO, CONCRETO, METRÔ, GAROA, NEON, AÇO, ASFALTO,
  SIRENE, BUZINA, MINHOCÃO, PAULISTA, TIETÊ, BÁSICA), sem aliasing (mipmaps por FFT), normalizadas por volume.
- SUB (seno ou quadrada, -1 ou -2 oitavas) e NOISE (branco, rosa, marrom, industrial, digital, vinil).
- Filtro MOOG LADDER (ZDF, afinado, auto-oscila) ou CLEAN SVF, 12/24 dB, LP/BP/HP, DRIVE, KEY, roteamento por fonte.
- 3 envelopes (ENV 1 é o volume), 3 LFOs (7 formas, sincronia com o BPM, FREE/RETRIG/ONE SHOT, SMOOTH).
- Matriz de modulação com 16 ligações, 21 origens (inclusive ACCENT), 30 destinos; 4 macros; arraste o ⊕ até um knob.
- FX: DIST (TAPE, TUBE, HARD, FOLD, CRUSH), CHORUS, DELAY (ping-pong, sincronizado), REVERB (FDN), COMP, limitador.
- SEQUENCER de 16 passos com GATE, NOTE, VELO, PROB, MOD 1-4 e, como no TB-303, **ACCENT** (mais alto e mais
  aberto, acumulando em acentos seguidos) e **SLIDE** (a nota segura e desliza para a próxima, sem novo ataque). ARP.
- 22 sons de fábrica em 4 bancos (A CONCRETE, B ASFALTO, C GAROA, D USINA).

## O plugin

O painel do plugin é o mesmo da web, num layout compacto de 1120 × 720 com abas SYNTH, MOD, FX, MATRIX,
SEQUENCER e GLOBAL (escala de 80 % a 150 %). Todos os knobs são automatizáveis na DAW, com os mesmos nomes.
O sequencer segue o tempo e o PLAY da DAW, alinhado ao compasso. Cada instância tem seu próprio motor.

Formatos: AU e VST3 no Mac (Apple silicon e Intel), VST3 no Windows e no Linux, e um app Standalone.
VST2 não existe mais para plugins novos (a Steinberg parou de licenciar em 2018); todas as DAWs atuais abrem VST3,
e o Logic abre AU. Como instalar: `plugin/INSTALAR-MAC.txt`, `plugin/INSTALAR-WINDOWS.txt`.

## O FM-1

`firmware/` é o firmware do FM-1: o SLOOP 2.7.1 (de isod89, baseado no Felucca de Leo Kuroshita, GPL-3.0) com o
motor analógico SELVA1 (2 osciladores com as ondas clássicas, sub, ruído, ladder 24 dB afinado, dois ADSR, 15 sons),
a marca Selva1 na abertura e no ABOUT. O GitHub compila o pacote `.fwsc` e a página do instalador (Web MIDI, Chrome
ou Edge). Detalhes em `firmware/SELVA1.md`.

## Estrutura

```
core/                o motor (C, sem libc): s1_params.h (a lista de parâmetros), s1_wt.c, selva1.c
tests/s1_test.c      testes no computador: extremos, afinação, sequencer, slide e accent, instâncias, custo
web/                 o painel: index.html, app.js, worklet.js, presets.js, fonts/
plugin/              o plugin (JUCE 8): CMakeLists.txt, src/
firmware/            o firmware do FM-1 (SLOOP + SELVA1)
tools/               build_wasm.sh, build.py (páginas), render_presets.mjs (volumes dos sons)
.github/workflows/   tudo compilado no GitHub; uma tag v* publica uma release
```

## Compilar à mão

```
sh tools/build_wasm.sh                     # motor -> build/selva1.wasm (clang com wasm32, wasm-ld)
cc -O2 -DS1_HOST -Icore -o build/s1_test tests/s1_test.c -lm && build/s1_test
python3 tools/build.py                     # páginas: dist/selva1 (site), dist/artifact.html, dist/plugin (plugin)
cmake -B build/plugin -S plugin && cmake --build build/plugin --config Release
```

## Licença

GPL-3.0 (veja `LICENSE`). O firmware do FM-1 deriva do SLOOP e do Felucca (GPL-3.0); o plugin usa o JUCE 8
(AGPLv3 ou licença comercial: a gratuita Starter cobre até US$ 20 mil de receita por ano). As fontes do painel
(Anton, DM Mono, Inter) são OFL.

Copyright (C) 2026 DeAbrew.
