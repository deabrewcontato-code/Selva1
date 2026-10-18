# Selva1: o synth analógico da DeAbrew no FM-1

O motor do Selva1 (no FM-1 ele aparece como SELVA1) é o motor 12 do firmware, depois do NOISE, escrito em C no mesmo estilo dos motores do SLOOP:
ponto fixo, 44,1 kHz, 8 vozes, estado só por voz (roda igual em qualquer um dos dois núcleos). O mesmo código vira o
firmware do FM-1 e o painel web (WebAssembly), então o som do navegador é o som do aparelho.

## Caminho do som (por voz)

```
OSC 1 + OSC 2 + SUB + RUÍDO -> MIXER (DRIVE) -> FILTRO LADDER 24 dB -> AMPLIFICADOR
                                              ^ ENVELOPE DO FILTRO        ^ ENVELOPE DO VOLUME (o ENV da faixa)
```

| Parte | O que faz |
|---|---|
| OSC 1, OSC 2 | 6 ondas: TRI, SHARK (triângulo-serra), SAW, SQR, WIDE (pulso 30 %), NARROW (pulso 12 %); faixa 32' a 2'; o OSC 2 tem SEMI (±12) e FINE (±50 cents). LFO / ENV -> SHP fazem PWM nas ondas de pulso |
| SUB | quadrada ou seno, 1 ou 2 oitavas abaixo do OSC 1 (segue a faixa do OSC 1) |
| RUÍDO | branco ou rosa |
| DRIFT | desafinação aleatória por nota, OSC 1 e OSC 2 cada um para um lado (até ±8 cents) |
| MIXER + DRIVE | os quatro níveis; o DRIVE empurra a soma para a saturação na entrada do filtro |
| FILTRO | ladder de 4 polos (24 dB/oitava), ênfase até auto-oscilação, perda de graves da ressonância compensada pela metade, afinado (auto-oscilação a ±15 cents até 4 kHz); KTR: OFF, 1/3, 2/3, FULL |
| F.ENV | ADSR próprio do filtro, ENV = quantidade (+ abre, - fecha) |
| Amplificador | o ADSR da faixa (página ENV), velocity, LFO -> AMP |

## Parâmetros

- `P_E0..P_E7` (projetos, presets de usuário, páginas EDIT do FM-1, knobs da HOME = CUT RES ENV DRV):
  CUT, RES, ENV, DRV | F.AT, F.DC, F.SU, F.RL. CC74 move o CUT.
- Osciladores e mixer: 14 valores por parte em `selva_x[]` (`eng_selva.c`, `SELVA_XD` descreve cada um). Hoje eles vêm
  dos presets de fábrica e do painel web; páginas próprias no FM-1 e o armazenamento em projeto são a etapa 2.

## Presets de fábrica (15)

INIT, MINI BASS, FAT LEAD, ACID LINE, RAW RUMBLE, TECHNO STB, BRASS, WARM PAD, PLUCK, SUB 808, PWM LEAD,
SHARK BASS, SQR KEYS, WIND, DRONE. Níveis igualados com `tools/level_presets.py` (linha 12 de `preset_trim.h`).

## Custo

Instruções por amostra no host (callgrind, uma parte com 8 notas, o preset mais pesado de cada motor):
Selva1 RAW RUMBLE 2810, ANALOG 2082, PHYS 2170, FM6 1912; a mistura de teste com 3 partes + bateria dá 2491.
Uma parte Selva1 com 8 vozes no pior caso custa mais ou menos o que a mistura completa de teste custa. Falta medir no
aparelho (página GLO > SYSTEM, coluna INFO).

## Testes

```
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/selva_test tests/selva_test.c -lm
build/host/selva_test build/selva     # extremos, afinação do filtro, envelope do filtro, demos em WAV
```

Também está em `tests/run_tests.sh`. A suíte de regressão do SLOOP continua com os 182 renders antigos idênticos;
o Selva1 acrescentou 15.

## Painel web

```
python3 tools/build_preview.py      # o DSP em WebAssembly (clang + wasm-ld, ou emcc)
python3 tools/build_selva_web.py      # build/selva-web/index.html + selva-worklet.js
```

Os dois arquivos podem ir para qualquer site (por exemplo `deabrewmusic.com.br/fm1/selva1/`). Precisa de Chrome ou Edge.

## O que muda em relação ao SLOOP

- `NENGINES` passou de 12 para 13 (SLICE, quando compilado, virou o 13). O protocolo do editor web do SLOOP usa
  `NENGINES` para dizer "faixa de bateria": o editor oficial do SLOOP vai ler errado a faixa de bateria deste
  firmware até ser ajustado (etapa 2).
- `eng_track_loaded()` (engines.c) chama `fm6_track_loaded()` e `selva_track_loaded()` onde antes só o FM6 era chamado.

Licença GPL-3.0, como o SLOOP e o Felucca.
