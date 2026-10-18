# Selva1 no M-VAVE FM-1 · como instalar

O firmware Selva1 para o FM-1 é o SLOOP 2.7.1 (groovebox de 4 faixas) com o motor **SELVA1**: 2 osciladores com as
ondas clássicas (TRI, SHARK, SAW, SQR, WIDE, NARROW), sub, ruído, filtro ladder 24 dB afinado, ADSR de filtro e de
volume, e 15 sons. Na abertura aparece **SELVA1**; o ABOUT (segure HOME > SYSTEM > ABOUT) mostra a versão.

## Instalar (Chrome ou Edge, num computador)

1. Abra **https://deabrewmusic.com.br/selva1/fm1/** no Chrome ou no Edge.
2. Ligue o FM-1 no computador com um cabo USB **de dados**, direto (sem hub). Feche a DAW e qualquer app que use MIDI.
3. Clique em **INSTALL**, permita o acesso ao MIDI e espere o **Done**. O FM-1 reinicia mostrando SELVA1.
4. Desligue e ligue o cabo uma vez, para o computador achar a entrada de áudio USB do FM-1.

Pelo terminal também dá: `python3 tools/fm1_install.py selva1-fm1-0.1.fwsc` (precisa de `pip3 install mido python-rtmidi`).

## Tocar o SELVA1

Numa faixa de synth (ALGORITHM 1, 2 ou 3), gire **PRESETS** até os sons do SELVA1 (MINI BASS, FAT LEAD, ACID LINE,
RAW RUMBLE, TECHNO STB, BRASS, WARM PAD, PLUCK, SUB 808, PWM LEAD, SHARK BASS, SQR KEYS, WIND, DRONE).
- **EDIT** página 1 (FLT): corte, ressonância, quantidade do envelope, drive.
- **EDIT** página 2 (FEG): o ADSR do filtro.
- **ENV**: o ADSR do volume.
- **HOME**: corte, ressonância, envelope e drive nos 4 knobs.

O sequencer, o ARP, o slide, os níveis por passo e os efeitos são os do SLOOP (veja `GUIDE.md`).

## Voltar atrás

- Para o SLOOP original ou o firmware oficial: no instalador, **Return to the official firmware (V15)** com o arquivo
  FM-1.fwsc da M-VAVE, ou o M-UPGRADE da M-VAVE. Salve um backup no editor antes.
- **Resgate**: se o FM-1 não abrir, segure **OCT−** enquanto liga (aparece SELVA1 USB RESCUE) e instale de novo.

Instalar firmware alternativo é por sua conta e risco (GPL-3.0, sem garantia). Este pacote passou em todas as
verificações do build do SLOOP (tamanho da imagem, memória, acesso a registradores) e o motor passou nos testes no
computador; a primeira instalação num aparelho é a sua.
