# espvcs (Atari 2600) -> TTGO VGA32

Port do emulador **espvcs** (Atari 2600, da familia MCUME do Jean-Marc
Harvengt) para a **LilyGO/TTGO VGA32**, montado enxertando o core de emulacao
do 2600 sobre a **camada de plataforma do teu esp64 (C64)** — a mesma que ja
roda VGA + PS/2 + SD + menu + ponte T-Display nessa placa.

> **Estado: primeiro corte montado, mirando compilar. NAO foi compilado nem
> testado aqui** (o sandbox nao tem toolchain/rede). Alem disso, a propria
> camada VGA do esp64 estava marcada como "nao verificada" no
> `README-VGA.md`. Ou seja: e' um ponto de partida para iterar na placa, como
> de costume — nao um binario pronto.

## Arquitetura

O espvcs e o esp64 compartilham a mesma abstracao `emuapi` do MCUME. Por isso
o core do 2600 fala com a plataforma pelas mesmas funcoes `emu_*`. O core do
2600 desenha o **quadro inteiro** de uma vez (`Vmachine.c` chama
`emu_DrawScreen(VBuf, 160, 192, 160)` + `emu_DrawVsync()` uma vez por quadro),
diferente do C64, que ia linha a linha.

### Reutilizado VERBATIM do esp64 (plataforma)
- `video_vga.cpp/.h` — FabGL VGADirectController, framebuffer 320x240 8bpp.
- `Ps2kbd.cpp/.h` — teclado PS/2 via FabGL (com 2 ajustes, ver abaixo).
- `port_link.cpp/.h` — ponte do gamepad da T-Display (UART2).
- `emuapi.cpp` — SD, catalogo de ROMs, menu OSD, paleta, input, audio glue
  (com 1 ajuste: `emu_DrawScreen`, ver abaixo).
- `keyboard_osd.h`, `iopins.h`, `gbConfig.h`, `go.h`, `font8x8.h`.

### Do proprio espvcs (core de emulacao 2600)
- `At2600.c Cpu.c Collision.c Display.c Exmacro.c Keyboard.c Memory.c
  Options.c Raster.c Table.c Tiasound.c Vcsemu.c Vmachine.c` + headers.
- `AudioPlaySystem.cpp/.h` — **mantido o do Atari**, que usa o caminho Arduino
  `esp32-hal-timer` + `dacWrite` (DAC1 = GPIO25) a ~22 kHz. Isso evita a mina
  do `dac_continuous` (IDF 5.x) do AudioPlaySystem do esp64, que nao compila na
  `espressif32@6.12.0`.

### Escrito/adaptado neste port
- `emuapi.h` — merge: flags de plataforma do esp64 + hooks `vcs_*` do 2600,
  `PALETTE_SIZE=256`, `ROMSDIR="2600"`, `CUSTOM_SND` desligado.
- `go.cpp` — entrypoint Arduino: mantem a infra validada do esp64 (ISR de
  video no core 1, emulador no core 0, otadata apagado no boot, dono unico do
  teclado, resync do menu) e troca o miolo por: `emu_Step()` = 1 quadro,
  cadencia ~60 Hz, **boot no menu** (o 2600 precisa de cartucho), switches de
  console passando direto para o core.
- `emuapi.cpp::emu_DrawScreen` — dobra cada pixel na horizontal (160->320,
  escala inteira, sem interpolacao) e centra na vertical (offset 24). Usa so'
  o `writeLine()` publico do `VGA_Video`.
- `Ps2kbd.cpp` — 2 casos novos: **F1 = SELECT** (USER2), **F2 = COLOR/BW**
  (USER3); e F9/F10/F11 remapeados para a semantica do 2600.
- `AudioPlaySystem` — construtor nao chama mais `begin()` no static-init;
  `begin()` ganhou guarda contra init duplo ao trocar de jogo.

### Descartado (era do C64/Teensy64, nao usado)
- `util.cpp/.h` (puxava `Teensy64.h`), `vga_6bit.cpp/.h` (o video usa FabGL,
  nao o driver bitluni), e todo o nucleo do C64 (vic/cia/pla/cpu/reSID).

## Controles

| Tecla PS/2 | Funcao 2600 |
|---|---|
| **F11** | switch **RESET** do console (inicia/reinicia a maioria dos jogos) |
| **F1**  | switch **SELECT** |
| **F2**  | switch **COLOR / B&W** |
| **F9**  | abre/fecha o **menu de ROMs** |
| **F10** | recarrega o cartucho atual |
| **F12** | liga/desliga o modo joystick por teclado (Q/A/O/P + SPACE) |

- **Joystick**: o gamepad da **T-Display** (pela ponte UART) entrega
  direcional + fire direto (`MASK_JOY2_*`), entao o 2600 fica jogavel sem PS/2.
  No PS/2, F12 ativa Q=cima A=baixo O=esq P=dir SPACE=fire.
- **Dificuldade P0/P1** (`nOptions_P1Diff/P2Diff` em `Options.c`) ainda nao tem
  tecla dedicada — ponto para amarrar depois, se algum jogo precisar.

## ROMs

Coloque os cartuchos (`.a26` / `.bin`) em **`/sdcard/2600`** no cartao.
O menu le essa pasta (subpastas suportadas, "`..`" para subir).

## Pontos de risco (ordem de suspeita)

1. **Camada VGA nunca compilada** — herdada do esp64, que marcou como nao
   verificada. Se travar no boot, use o `ISOLATION_TEST` (existia no go.cpp do
   esp64; posso reintroduzir) para isolar SPI vs video vs emu.
2. **Geometria/paleta na tela** — o 2600 usa 160x192 (NTSC). Se um jogo for
   PAL, `tv_height` vira 228 e o centro vertical muda sozinho (offset 6). Cor
   errada = ordem RGB da paleta / `RGBVAL16`.
3. **Cartucho nao inicia** — quase todo jogo do 2600 so' arranca ao pulsar o
   switch RESET (F11). Se a tela ficar parada, tente F11 primeiro.
4. **Audio** — caminho Arduino timer+DAC (GPIO25). Se nao sair som, confira o
   `DacPin=25` e o `timerAlarmWrite(45)` no `AudioPlaySystem.cpp`.
5. **Particao / bootloader** — o `go.cpp` apaga o `otadata` no boot para voltar
   a' factory (teu bootloader) no proximo reset. Ajuste `board_build.partitions`
   no `platformio.ini` para o **mesmo CSV** que teus outros emuladores usam
   (factory + ota + otadata); o `huge_app.csv` esta la' so' como ponto de
   partida.
6. **Case-sensitivity** — alguns includes do core do 2600 sao minusculos
   (`config.h`, `vcsemu.h`, `memory.h`) e resolvem no macOS por o FS ser
   case-insensitive. Se um dia compilar no Linux/CI, vai reclamar.

## Build

Abra a pasta na **PlatformIO (VS Code)** e compile o env `vga32`. Sem
dependencia de `pio` na linha de comando.
