#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "go.h"

extern "C" {
  #include "emuapi.h"
  #include "iopins.h"
}

// emuapi.cpp: zera bLastState para evitar bordas fantasma ao abrir o menu.
extern void emu_ResetKeyState(void);

// emuapi.cpp: telemetria do blit -- devolve us acumulados no emu_DrawScreen e
// a contagem de quadros desenhados desde a ultima chamada (e zera).
extern "C" void emu_perfGetDraw(int64_t * us, int * count);

#include "keyboard_osd.h"
#ifdef HAS_PS2KBD
#include "ps2kbd.h"
int ps2kbd_get_joy_mode(void);   // fwd decl caso o header nao exponha
#endif
#include "video_vga.h"
#ifdef HAS_TDISPLAY_LINK
#include "port_link.h"
#endif
#ifdef HAS_SND
#include "AudioPlaySystem.h"
#endif

// ===========================================================================
// espvcs (Atari 2600) na TTGO VGA32
//
// Estrutura herdada do esp64 (C64), que carrega as correcoes validadas em
// hardware -- ISR de video no core 1, emulador no core 0, "dono unico do
// teclado", resync do menu, apagar otadata no boot. O que muda para o 2600:
//
//   * emu_Step() (= vcs_Step -> mainloop) roda UM QUADRO INTEIRO, nao uma
//     linha de raster. A cadencia e' por quadro (~60 Hz NTSC), bem mais
//     simples que a do C64 (312 linhas/quadro).
//   * o 2600 nao arranca "no vazio": precisa de um cartucho. Entao o boot cai
//     no MENU de ROMs, em vez de startGame("") como o C64 (que ia pro BASIC).
//   * os switches de console (RESET/SELECT/COLOR = USER1/2/3) NAO sao
//     consumidos aqui: fluem por emu_ReadKeys() ate' Keyboard.c::keycons(),
//     que os le a cada quadro.
// ===========================================================================

VGA_Video video;
#ifdef HAS_SND
AudioPlaySystem audio;
#endif

// Stub do C64: o video_vga.cpp (herdado do esp64) chama vic_get_border_color()
// dentro do flushLine(), que so' e' usado pelo caminho getLineBuffer() do
// VIC-II. O Atari 2600 desenha por emu_DrawScreen()->writeLine(), entao
// flushLine() nunca roda aqui -- mas ainda precisa LINKAR. Borda preta.
extern "C" uint16_t vic_get_border_color(void) { return 0; }

// Ultimo jogo carregado, para o F10 (recarregar/"reset" do 2600).
static char s_lastGame[64] = {0};

#define HOTKEY_TRACE 0

// ---- DONO UNICO DO TECLADO (ver esp64/go.cpp) ----------------------------
static uint16_t s_prevKeys = 0;

static uint16_t keys_edge(void)
{
  uint16_t keys = emu_ReadKeys();
  uint16_t edge = keys & ~s_prevKeys;
  s_prevKeys = keys;
  return edge;
}

static void keys_resync(void)
{
#ifdef HAS_PS2KBD
  while (ps2kbd_get_events()) {}   // drena a fila de eventos
#endif
  s_prevKeys = emu_ReadKeys();
  emu_ResetKeyState();
}

// Audio + link. NAO le teclado (dono unico = a emuthread).
static void input_task(void *args)
{
  while (true) {
#ifdef HAS_TDISPLAY_LINK
    link_poll();
#endif
#ifdef HAS_SND
    audio.step();
#endif
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

static bool inputTaskStarted = false;

// Sai do menu e roda o cartucho selecionado.
static void startGame(char *filename)
{
#ifdef HAS_SND
  audio.begin();
  audio.start();
#endif
  toggleMenu(false);
  video.fillScreenNoDma( RGBVAL16(0x00,0x00,0x00) );

  if (!inputTaskStarted) {
    xTaskCreatePinnedToCore(input_task, "inputthread", 4096, NULL, 2, NULL, 1);
    inputTaskStarted = true;
  }

  // guarda o nome para o F10 (recarregar)
  strncpy(s_lastGame, filename, sizeof(s_lastGame)-1);
  s_lastGame[sizeof(s_lastGame)-1] = 0;

#ifdef HAS_TDISPLAY_LINK
  link_send_game_name(filename);
#endif
  emu_Init(filename);          // = vcs_Init(); vcs_Start(filename);
#ifdef HAS_TDISPLAY_LINK
  g_menuRequest = false;
#endif
  keys_resync();
}

static void openMenu(void)
{
  toggleMenu(true);
  keys_resync();
}

static void closeMenu(void)
{
  toggleMenu(false);
  keys_resync();
}

// ---- Teclas do JOGO: uma vez por quadro, so' na emuthread ------------------
// So' interceptamos F9 (menu) e F10 (recarregar). Os switches de console
// (USER1/2/3) NAO sao tratados aqui: seguem para keycons() via emu_ReadKeys().
static void keys_step(void)
{
  uint16_t edge = keys_edge();
  if (!edge) return;

#if HOTKEY_TRACE
  printf("[hot] edge=%04X\n", edge); fflush(stdout);
#endif

  if (edge & MASK_KEY_MENU) {          // F9
    openMenu();
    return;
  }
  if (edge & MASK_KEY_RESET) {         // F10: recarrega o cartucho atual
    if (s_lastGame[0]) {
      char tmp[64];
      strncpy(tmp, s_lastGame, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
      emu_Init(tmp);
      keys_resync();
    }
    return;
  }
}

static void main_step(void)
{
  if (menuActive()) {
#ifdef HAS_TDISPLAY_LINK
    link_poll();
#endif
    uint16_t bClick = emu_GetMenuKeys();

    // F9 fecha o menu (o bit MENU nao chega em bClick: e' hotkey).
    {
      static bool s_menuF9held = false;
      bool f9now = (emu_ReadKeys() & MASK_KEY_MENU) != 0;
      if (f9now && !s_menuF9held) { s_menuF9held = true; closeMenu(); return; }
      if (!f9now) s_menuF9held = false;
    }

    int action = handleMenu(bClick);
    char *filename = menuSelection();
    if (action == ACTION_RUN) {
      startGame(filename);
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
  else {
#ifdef HAS_TDISPLAY_LINK
    if (g_menuRequest) {
      g_menuRequest = false;
      openMenu();
      return;
    }
#endif
    emu_Step();     // roda UM quadro do 2600
  }
}

// ===========================================================================
void emu_setup(void)
{
  printf("Starting emulator (Atari 2600)\n"); fflush(stdout);

#ifdef HAS_TDISPLAY_LINK
  link_init();
#endif

  video.begin();          // no-op se ja' inicializado no setup() (core 1)
  video.flipscreen(true);
  video.start();
  video.refresh();

  emu_init();             // monta SD, le catalogo de /sdcard/2600, PS/2, etc.

  // O 2600 nao roda sem cartucho: abre o menu de ROMs no boot.
  printf("setup: abrindo o menu de ROMs\n"); fflush(stdout);
  openMenu();
}

void emu_loop(void)
{
  bool    wasMenu = menuActive();
  int64_t _w0 = esp_timer_get_time();
  main_step();
  int64_t _w1 = esp_timer_get_time();

  // ---- Cadencia de ~60 Hz (NTSC) -----------------------------------------
  // vcs_Step() emula UM quadro inteiro e retorna. Sem cadencia, o emulador
  // rodaria o mais rapido que a CPU deixasse. Dormimos o que sobrar de cada
  // janela de 16.667 ms. So' aplica quando NAO estamos no menu (la' o
  // main_step ja' tem seu proprio vTaskDelay).
  if (!menuActive()) {
    static int64_t nextFrame = 0;
    int64_t now = esp_timer_get_time();
    if (nextFrame == 0) nextFrame = now;
    nextFrame += 16667;                       // 1/60 s
    int64_t wait = nextFrame - now;
    if (wait > 1000) {
      vTaskDelay((wait / 1000) / portTICK_PERIOD_MS);
    } else if (wait < -200000) {
      nextFrame = now;                        // muito atrasado: ressincroniza
    }

    // Teclas do jogo: UMA leitura por quadro, nesta task, neste core.
    keys_step();

#ifdef HAS_PS2KBD
    // Indicador de modo joystick (F12) no canto inferior direito.
    int jm = ps2kbd_get_joy_mode();
    if (jm == 1)
      video.drawTextNoDma(304, 232, "J1", RGBVAL16(0x00,0xff,0x00), RGBVAL16(0,0,0), false);
    else if (jm == 2)
      video.drawTextNoDma(304, 232, "J2", RGBVAL16(0xff,0xff,0x00), RGBVAL16(0,0,0), false);
#endif

    // ---- TELEMETRIA (1x por segundo) ---------------------------------------
    // Mede quanto de cada quadro e' trabalho de CPU (main_step = core do 2600
    // + blit) e separa o blit (emu_DrawScreen) do resto. O FPS usa relogio de
    // parede (inclui a espera da cadencia), entao:
    //   - se aparecer "CPU-BOUND": o quadro custa >16.7ms, nao ha' sono, e o
    //     FPS real e' o que a CPU aguenta. Olhe se domina "2600=" (core rodando
    //     da flash com -Os) ou "video=" (o blit de 61k pixels/quadro).
    //   - se aparecer "com folga" mas o FPS estiver baixo, o gargalo esta' fora
    //     do main_step (ex.: a propria cadencia, ou a ISR de video no core 1).
    if (!wasMenu) {
      static uint32_t s_frames   = 0;
      static int64_t  s_workAcc  = 0;
      static int64_t  s_winStart = 0;

      s_frames++;
      s_workAcc += (_w1 - _w0);
      if (s_winStart == 0) s_winStart = _w0;

      int64_t span = esp_timer_get_time() - s_winStart;
      if (span >= 1000000) {                     // ~1 s
        int64_t drawUs = 0; int drawCnt = 0;
        emu_perfGetDraw(&drawUs, &drawCnt);

        float secs    = span / 1000000.0f;
        float fps     = s_frames / secs;
        float frameMs = (s_workAcc / (float)s_frames) / 1000.0f;
        float drawMs  = drawCnt ? (drawUs / (float)drawCnt) / 1000.0f : 0.0f;
        float coreMs  = frameMs - drawMs;        // core do 2600, sem o blit

        printf("[tel] FPS=%.1f  quadro=%.2fms (2600=%.2f + video=%.2f)  %s  heapDMA=%u free=%u\n",
               fps, frameMs, coreMs, drawMs,
               (frameMs >= 16.7f) ? "CPU-BOUND" : "com folga",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
               (unsigned)ESP.getFreeHeap());
        fflush(stdout);

        s_frames = 0; s_workAcc = 0; s_winStart = 0;
      }
    }
  }

  // Rede de seguranca do watchdog.
  static int64_t lastYield = 0;
  int64_t now = esp_timer_get_time();
  if (now - lastYield > 50000) { lastYield = now; vTaskDelay(1); }
}

// ---------------------------------------------------------------------------
static void emu_task(void *arg)
{
  Serial.printf("emuthread no core %d\n", (int)xPortGetCoreID());
  Serial.flush();
  emu_setup();
  while (1) emu_loop();
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== MCUME espvcs (Atari 2600) - TTGO VGA32 ===");
  Serial.println("PS/2:  F9=menu  F10=recarrega  F11=RESET  F1=SELECT  F2=COLOR/BW  F12=joystick");
  Serial.println("Joystick: gamepad da T-Display, ou Q/A/O/P/SPACE no PS/2 (F12).");

  // Integracao com o bootloader (fg1998/esp32-bootloader): apagar o otadata
  // faz o ESP32 voltar para a particao factory no proximo boot em vez de
  // recarregar este emulador. Cedo, antes de qualquer periferico.
  {
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
      esp_partition_erase_range(otadata, 0, otadata->size);
      Serial.println("otadata apagado: proximo boot vai para a factory");
    } else {
      Serial.println("AVISO: sem particao otadata (veja board_build.partitions)");
    }
    Serial.flush();
  }

  Serial.printf("PSRAM: %s  livre=%u\n",
                psramFound() ? "detectada" : "NAO detectada",
                (unsigned)ESP.getFreePsram());
  Serial.printf("Heap interno livre=%u  DMA livre=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
  Serial.printf("Compilado: %s %s\n", __DATE__, __TIME__);

  // A FabGL aloca a ISR de scanline no core onde setResolution() roda. setup()
  // do Arduino roda na loopTask (core 1) -- inicializar o video AQUI deixa a
  // ISR no core 1 e libera o core 0 para o emulador.
  video.begin();

  xTaskCreatePinnedToCore(emu_task, "emuthread", 16384, NULL, 1, NULL, 0);
}

void loop()
{
  // NAO le teclado (dono unico = emuthread).
  vTaskDelay(50 / portTICK_PERIOD_MS);
}