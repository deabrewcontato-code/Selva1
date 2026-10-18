# Selva1

Synth analógico inspirado na selva de pedra de São Paulo, para o **M-VAVE FM-1** e para o **navegador**. O mesmo
código em C roda no firmware do aparelho e no painel web (WebAssembly): o som que você ajusta na tela é o som que sai
do FM-1.

Um projeto da [DeAbrew](https://deabrewmusic.com.br), aberto (GPL-3.0) e construído sobre o
[SLOOP](https://github.com/isod89/sloop-fm1) (isod89), que por sua vez vem do
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita / Hügelton Instruments).

*English: Selva1 is an analog-style synth voice for the M-VAVE FM-1 and the browser, built on the SLOOP firmware.
Issues and pull requests in English are welcome.*

## Situação

**Etapa 1 pronta: o motor e o painel web.** O firmware compila no simulador e passa em todos os testes, mas ainda
não foi instalado num FM-1 de verdade. Firmware alternativo: instale por sua conta e risco, sempre com backup.

| Etapa | O que é | Estado |
|---|---|---|
| 1 | Motor analógico (2 osciladores, sub, ruído, filtro 24 dB, dois ADSR), 15 presets, painel web | pronta |
| 2 | Build e teste no FM-1, páginas dos osciladores no aparelho, salvar em projeto, mandar patch da web pelo USB | próxima |
| 3 | Identidade visual do Selva1 (painel, tela do FM-1, logo) | depois |

## O som

```
OSC 1 + OSC 2 + SUB + RUÍDO -> MIXER (DRIVE) -> FILTRO LADDER 24 dB -> AMPLIFICADOR
```

- **2 osciladores** com as 6 ondas clássicas: triângulo, triângulo-serra, serra, quadrada, pulso largo e pulso
  estreito; faixa 32' a 2'; o OSC 2 tem semitom e fino.
- **Sub** (quadrada ou seno, uma ou duas oitavas abaixo), **ruído** branco ou rosa, **drift** por nota.
- **Filtro ladder 24 dB** afinado, com ênfase até auto-oscilar, drive na entrada e keytrack OFF, 1/3, 2/3, TOTAL.
- **Dois ADSR**: filtro e volume. Mais LFO, glide, poly/mono/legato/uníssono e os efeitos do SLOOP.

Detalhes técnicos, parâmetros e custo de processador: [SELVA1.md](SELVA1.md).

## Onde está cada coisa

| Caminho | O quê |
|---|---|
| `firmware/src/eng_selva.c` | o motor (DSP, parâmetros, presets) |
| `web/selva/` | o painel web (`panel.html`) e o processador de áudio (`worklet.js`) |
| `tools/build_selva_web.py` | monta o painel num HTML só, com o DSP dentro |
| `tests/selva_test.c` | testes do motor e os WAVs de demonstração |
| `firmware/host/preview.c` | a ponte entre o DSP e o navegador |
| o resto | o SLOOP 2.7.1 como ele é ([README-SLOOP.md](README-SLOOP.md)) |

## Rodar o painel web

Precisa de Python 3, um compilador C e clang com `wasm-ld` (ou Emscripten). Não precisa do FM-1.

```sh
python3 tools/build_preview.py          # o DSP do firmware em WebAssembly
python3 tools/build_selva_web.py        # build/selva-web/index.html + selva-worklet.js
python3 -m http.server 8766 --directory build/selva-web
```

Abra `http://localhost:8766` no Chrome ou no Edge e clique em **Ligar o som**.

## Compilar o firmware

Igual ao SLOOP: compilador JieLi (Linux x86-64) e três arquivos do SDK AC79. Veja [BUILDING.md](BUILDING.md).

## Testes

```sh
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/selva_test tests/selva_test.c -lm
build/host/selva_test build/selva       # extremos, afinação do filtro, envelope, demos em WAV
tests/run_tests.sh                       # a suíte inteira (depois de ./build.sh)
```

A regra da suíte vale para o Selva1: nenhum som existente muda sem querer. Os 182 renders do SLOOP continuam
idênticos (`tests/golden.txt`).

## Como participar

1. Abra uma *issue* contando a ideia ou o problema (som, painel, firmware, documentação).
2. Trabalhe num branch a partir do `main` e mande um *pull request*.
3. Antes do pull request: rode os testes, e se o som mudou de propósito, atualize os renders com
   `GOLDEN_UPDATE=1` e explique a mudança.
4. Código em C no estilo do SLOOP: ponto fixo, sem alocação, estado por voz.

## Licença e créditos

GPL-3.0, como o SLOOP e o Felucca ([LICENSE](LICENSE), [LICENSING.md](LICENSING.md)). O Selva1 é da DeAbrew,
escrito com o Claude. SLOOP: isod89. Felucca: Leo Kuroshita / Hügelton Instruments. Segundo núcleo: Charles Vestal (X0X),
Kerem Kilic (Melodee), Andre Rezemini (sloop-lab).
