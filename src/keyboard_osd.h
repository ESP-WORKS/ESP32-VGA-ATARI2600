#ifndef keyboard_osd_h_
#define keyboard_osd_h_
    
extern bool virtualkeyboardIsActive(void);
extern void drawVirtualkeyboard(void);
extern void toggleVirtualkeyboard(bool keepOn);
extern void handleVirtualkeyboard(void);

extern bool callibrationActive(void);
extern int  handleCallibration(uint16_t bClick);

extern bool menuActive(void);
extern char * menuSelection(void);
extern void toggleMenu(bool on);
extern int  handleMenu(uint16_t bClick);

// Tela de Help (F1): overlay que pausa a emulacao e mostra os atalhos.
// Padrao similar ao menu, mas mais simples: sem interacao alem de "fechar".
extern bool helpActive(void);
extern void toggleHelp(bool on);
extern void drawHelp(void);


#endif