#ifndef IHM_H
#define IHM_H

#include <stdint.h>

void initIHM();
void updateIHM();

// Couleurs RGB565 communes
constexpr uint16_t COLOR_BG       = 0x10A3;
constexpr uint16_t COLOR_CARD     = 0x1926;
constexpr uint16_t COLOR_BORDER   = 0x32CB;
constexpr uint16_t COLOR_ACCENT   = 0x5EF5;
constexpr uint16_t COLOR_TEXT     = 0xFFFF;
constexpr uint16_t COLOR_MUTED    = 0x94F6;
constexpr uint16_t COLOR_SELECTED = 0x1B69;

// Menu principal
void drawHome();
void handleHomeTouch(int x, int y);

// Ecrans
void drawScanner();
void drawRawViewer();
void drawFskViewer();
void drawAnalyzer();
void drawSavedSignals();
void drawSettings();

// Elements graphiques communs
void drawScreenFrame(
    const char* title,
    const char* description
);

#endif