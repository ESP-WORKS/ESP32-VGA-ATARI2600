#ifndef EMUAPI_H
#define EMUAPI_H

// ===========================================================================
// emuapi.h  --  MCUME espvcs (Atari 2600) portado para a TTGO VGA32
//
// Feito a partir do emuapi.h do esp64 (C64): mesma camada de plataforma
// (video FabGL VGADirectController, teclado PS/2, ponte T-Display, cartao SD,
// menu OSD, integracao com o bootloader). O que muda em relacao ao C64:
//
//   - hooks do core: vcs_Init/vcs_Start/vcs_Step (Vcsemu.c), nao c64_*
//   - PALETTE_SIZE = 256 (o 2600 usa colortable[256]; o C64 usava 16)
//   - CUSTOM_SND DESLIGADO: o Tiasound.c chama emu_sndPlaySound() direto, e o
//     AudioPlaySystem.cpp (do proprio Atari, caminho Arduino timer+DAC) usa o
//     mixer interno. Ligar CUSTOM_SND desviaria para SND_Process(), que o
//     2600 nao tem.
//   - sem macro de teclado (emu_Input vazio): o 2600 so' tem joystick + os
//     switches de console, lidos em Keyboard.c via emu_ReadKeys()/emu_GetPad().
// ===========================================================================

#define INVX        1
//#define INVY        1
#define HAS_SND     1
// CUSTOM_SND FICA DESLIGADO -- ver cabecalho acima.
//#define CUSTOM_SND  1
// GPIO 4/5 viraram B0/B1 do VGA. Sem teclado I2C nesta placa.
//#define HAS_I2CKBD  1

// Teclado PS/2 da VGA32 (CLK=33, DAT=32) via FabGL.
#define HAS_PS2KBD  1

// Joystick analogico desligado: PIN_JOY2_A2Y era ADC2_CHANNEL_2 = GPIO2, que
// na VGA32 e' o MISO do cartao SD, e PIN_JOY2_BTN era GPIO32, que agora e' o
// DAT do PS/2. O direcional vem pela ponte T-Display (ou por Q/A/O/P no PS/2).
#define NO_ANALOG_JOYSTICK 1
//#define USE_WIRE    1
// TTGO T-Display bluetooth controller bridge over UART2.
// Costs PIN_KEY_USER2 (GPIO34, becomes the link RX) and moves audio to DAC1.
#define HAS_TDISPLAY_LINK 1


// Title:     <                                        >
#define TITLE "         Atari 2600 (espvcs) VGA32       "
#define ROMSDIR "2600"

// Frequencia do SPI do cartao SD. 4000 costuma estabilizar em soquete de
// placa de dev; suba depois que tudo estiver funcionando.
#define SD_FREQ_KHZ 4000

// ---- Hooks do core do Atari 2600 (Vcsemu.c) -------------------------------
extern void vcs_Init(void);
extern void vcs_Start(char * filename);
extern void vcs_Step(void);

#define emu_Init(ROM) {vcs_Init(); vcs_Start(ROM);}
// "Reset" da maquina emulada. O 2600 nao tem um reset de software limpo como
// o C64 (o botao RESET real e' um switch de console, tratado em Keyboard.c via
// MASK_KEY_USER1). Aqui apenas re-inicializamos a maquina; go.cpp prefere
// recarregar o jogo atual no F10. Mantido para compatibilidade da API.
#define emu_Reset()   {vcs_Init();}
#define emu_Step()    {vcs_Step();}
// O 2600 nao tem teclado: nada a injetar.
#define emu_Input(x)  {}

#define VID_FRAME_SKIP       0x0
#define PALETTE_SIZE         256
#define SINGLELINE_RENDERING 1
#define VBUFFER_YCROP        0

#define ACTION_NONE          0
#define ACTION_MAXKBDVAL     4
#define ACTION_EXITKBD       128
#define ACTION_RUN           129

#ifdef KEYMAP_PRESENT

#define TAREA_W_DEF          32
#define TAREA_H_DEF          32
#define TAREA_END            255
#define TAREA_NEW_ROW        254
#define TAREA_NEW_COL        253
#define TAREA_XY             252
#define TAREA_WH             251

#define KEYBOARD_X           16
#define KEYBOARD_Y           32
#define KEYBOARD_KEY_H       30
#define KEYBOARD_KEY_W       28
#define KEYBOARD_HIT_COLOR   RGBVAL16(0xff,0x00,0x00)

// A VGA32 nao tem touchscreen: o teclado virtual/captureTouchZone nunca
// dispara (video.isTouching() e' stub = false). Estes arrays existem apenas
// para o emuapi.cpp compilar; sao inofensivos.
const unsigned short keysw[] = {
  TAREA_XY,KEYBOARD_X,KEYBOARD_Y,
  TAREA_WH,KEYBOARD_KEY_W,KEYBOARD_KEY_H,
  TAREA_NEW_ROW,40,40,140,40,40,
  TAREA_END};

const unsigned short keys[] = {
  1,2,ACTION_NONE,3,4
  };

#endif

#define MASK_JOY2_RIGHT 0x0001
#define MASK_JOY2_LEFT  0x0002
#define MASK_JOY2_UP    0x0004
#define MASK_JOY2_DOWN  0x0008
#define MASK_JOY2_BTN   0x0010
// No 2600 os switches de console entram por estes bits (ver Keyboard.c::keycons):
//   MASK_KEY_USER1 = RESET (inicia/reinicia o jogo)   -- F11 no PS/2
//   MASK_KEY_USER2 = SELECT                            -- F1  no PS/2
//   MASK_KEY_USER3 = COLOR / B&W                       -- F2  no PS/2
#define MASK_KEY_USER1  0x0020
#define MASK_KEY_USER2  0x0040
#define MASK_KEY_USER3  0x0080
#define MASK_JOY1_RIGHT 0x0100
#define MASK_JOY1_LEFT  0x0200
#define MASK_JOY1_UP    0x0400
#define MASK_JOY1_DOWN  0x0800
#define MASK_JOY1_BTN   0x1000
#define MASK_KEY_USER4  0x2000
#define MASK_KEY_MENU   0x4000  // F9 no PS/2: abre/fecha o menu de ROMs
#define MASK_KEY_RESET  0x8000  // F10 no PS/2: recarrega o jogo atual (go.cpp)


extern void emu_init(void);
extern void emu_printf(char * text);
extern void emu_printi(int val);
extern void * emu_Malloc(int size);
extern void emu_Free(void * pt);

extern int emu_FileOpen(char * filename);
extern int emu_FileRead(char * buf, int size);
extern unsigned char emu_FileGetc(void);
extern int emu_FileSeek(int seek);
extern void emu_FileClose(void);
extern int emu_FileSize(char * filename);
extern int emu_LoadFile(char * filename, char * buf, int size);
extern int emu_LoadFileSeek(char * filename, char * buf, int size, int seek);

extern void emu_InitJoysticks(void);
extern int emu_SwapJoysticks(int statusOnly);
extern unsigned short emu_DebounceLocalKeys(void);
extern unsigned short emu_GetMenuKeys(void);
extern int emu_ReadKeys(void);
extern int emu_GetPad(void);
extern int emu_ReadAnalogJoyX(int min, int max);
extern int emu_ReadAnalogJoyY(int min, int max);
extern int emu_ReadI2CKeyboard(void);
extern int emu_setKeymap(int index);

extern void emu_sndInit();
extern void emu_sndPlaySound(int chan, int volume, int freq);
extern void emu_sndPlayBuzz(int size, int val);

extern void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index);
extern void emu_DrawScreen(unsigned char * VBuf, int width, int height, int stride);
extern void emu_DrawLine(unsigned char * VBuf, int width, int height, int line);
extern void emu_DrawVsync(void);
extern int emu_FrameSkip(void);
extern void * emu_LineBuffer(int line);

#endif
