## Selva1 · o sintetizador da DeAbrew

**Baixe o arquivo do seu sistema** (em *Assets*, logo abaixo):

| Arquivo | O que é |
| --- | --- |
| `Selva1-mac.zip` | Mac (Apple silicon e Intel, macOS 11 ou mais novo): **Audio Unit** (Logic, GarageBand, Ableton), **VST3** e o app Standalone |
| `Selva1-windows.zip` | Windows 10/11 (64 bits): **VST3** e o app Standalone |
| `selva1-fm1-0.1.fwsc` | firmware para o **M-VAVE FM-1** |
| `Selva1-FM1-instalador.zip` | a página do instalador do FM-1 (a mesma que está em https://deabrewmusic.com.br/selva1/fm1/) |

### Mac, em 3 passos
1. Descompacte `Selva1-mac.zip`.
2. Copie `Selva1.component` para `~/Library/Audio/Plug-Ins/Components` e `Selva1.vst3` para `~/Library/Audio/Plug-Ins/VST3`
   (no Finder: menu **Ir > Ir para a Pasta…** e cole o caminho).
3. No Terminal, libere os arquivos (o plugin ainda não tem assinatura paga da Apple):
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Selva1.component ~/Library/Audio/Plug-Ins/VST3/Selva1.vst3
   ```
   Depois abra a DAW e faça um novo scan de plugins. Passo a passo completo: `INSTALAR-MAC.txt`, dentro do zip.

### Windows
Copie `Selva1.vst3` para `C:\Program Files\Common Files\VST3` e faça um novo scan na DAW (`INSTALAR-WINDOWS.txt`).

### FM-1
Abra https://deabrewmusic.com.br/selva1/fm1/ no Chrome ou no Edge, ligue o FM-1 por USB e clique em **INSTALL**.
Guia: `firmware/INSTALAR-FM1.md`.

### Novidades
- Plugin com painel compacto (1120 × 720, escala de 80 % a 150 %) em abas: SYNTH, MOD, FX, MATRIX, SEQUENCER, GLOBAL.
- Sequencer com **ACCENT** e **SLIDE** como no TB-303; segue o tempo e o PLAY da DAW.
- Todos os knobs automatizáveis na DAW.
- Firmware do FM-1 com o motor SELVA1 e a marca Selva1.

Validado no GitHub: `auval` (Audio Unit) e `pluginval` nível 5 (VST3, Mac e Linux).
