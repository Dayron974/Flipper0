#include "ihm.h"
#include <M5Unified.h>

// ==================================================
// Couleurs RGB565
// ==================================================

constexpr uint16_t COLOR_BG       = 0x10A3;
constexpr uint16_t COLOR_CARD     = 0x1926;
constexpr uint16_t COLOR_BORDER   = 0x32CB;
constexpr uint16_t COLOR_ACCENT   = 0x5EF5;
constexpr uint16_t COLOR_TEXT     = 0xFFFF;
constexpr uint16_t COLOR_MUTED    = 0x94F6;
constexpr uint16_t COLOR_SELECTED = 0x1B69;

// ==================================================
// Structure d'un bouton
// ==================================================

struct Button {
    int x;
    int y;
    int w;
    int h;

    const char* title;
    const char* subtitle;
    
    Screen target;

    bool contains(int tx, int ty) const {
        return tx >= x && tx < x + w &&
               ty >= y && ty < y + h;
    }
};

// ==================================================
// Liste des boutons
// ==================================================

Button buttons[] = {
    {0, 0, 0, 0, "Frequency Scanner",
     "RSSI / Frequency", Screen::SCANNER},

    {0, 0, 0, 0, "Read RAW",
     "OOK pulse capture", Screen::RAW_VIEWER},

    {0, 0, 0, 0, "Read FSK",
     "Packet receiver", Screen::FSK_VIEWER},

    {0, 0, 0, 0, "Signal Analysis",
     "Timing / Decoder", Screen::ANALYZER},

    {0, 0, 0, 0, "Saved Signals",
     "microSD storage", Screen::SAVED_SIGNALS},

    {0, 0, 0, 0, "Settings",
     "Radio / Diagnostics", Screen::SETTINGS}
};

constexpr int BUTTON_COUNT =
    sizeof(buttons) / sizeof(buttons[0]);

// ==================================================
// Calcul des positions des boutons
// ==================================================

void layoutButtons() {
    int screenW = M5.Display.width();
    int screenH = M5.Display.height();

    const int margin = 40;
    const int gap = 22;
    const int startY = 150;
    const int footer = 100;

    const int cols = 2;
    const int rows = 3;

    int buttonW =
        (screenW - 2 * margin - gap) / cols;

    int buttonH =
        (screenH - startY - footer
         - (rows - 1) * gap) / rows;

    for (int i = 0; i < BUTTON_COUNT; i++) {

        int col = i % cols;
        int row = i / cols;

        buttons[i].x = margin + col * (buttonW + gap);
        buttons[i].y = startY + row * (buttonH + gap);

        buttons[i].w = buttonW;
        buttons[i].h = buttonH;
    }
}

// ==================================================
// Dessin d'un bouton
// ==================================================

void drawButton(const Button& b, bool selected) {

    uint16_t background =
        selected ? COLOR_SELECTED : COLOR_CARD;

    uint16_t border =
        selected ? COLOR_ACCENT : COLOR_BORDER;

    // Fond
    M5.Display.fillRoundRect(
        b.x, b.y, b.w, b.h, 14, background
    );

    // Contour
    M5.Display.drawRoundRect(
        b.x, b.y, b.w, b.h, 14, border
    );

    // Titre
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(COLOR_TEXT);

    M5.Display.drawString(
        b.title,
        b.x + 28,
        b.y + 30
    );

    // Description
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_MUTED);

    M5.Display.drawString(
        b.subtitle,
        b.x + 28,
        b.y + 78
    );
}

// ==================================================
// En-tete
// ==================================================

void drawHeader() {

    M5.Display.setTextSize(5);
    M5.Display.setTextColor(COLOR_ACCENT);
    M5.Display.drawString("FLIPPER 433", 40, 25);

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_MUTED);

    M5.Display.drawString(
        "M5Stack Tab5 - Sub-GHz Analyzer",
        40, 85
    );

    M5.Display.drawString(
        "UI",
        M5.Display.width() - 150, 40
    );

    M5.Display.drawFastHLine(
        40, 116,
        M5.Display.width() - 80,
        COLOR_BORDER
    );
}

// ==================================================
// Barre d'etat
// ==================================================

void drawFooter() {

    int y = M5.Display.height() - 68;

    M5.Display.drawFastHLine(
        40, y,
        M5.Display.width() - 80,
        COLOR_BORDER
    );

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_MUTED);

    M5.Display.drawString(
        "433 MHz - IHM mode",
        40, y + 24
    );

    M5.Display.setTextColor(COLOR_ACCENT);

    M5.Display.drawString(
        "Standby",
        M5.Display.width() - 140,
        y + 24
    );
}

// ==================================================
// Dessin du menu complet
// ==================================================

void drawHome() {

    layoutButtons();

    M5.Display.fillScreen(COLOR_BG);

    drawHeader();

    for (int i = 0; i < BUTTON_COUNT; i++) {
        drawButton(buttons[i], false);
    }

    drawFooter();
}

// ==================================================
// Gestion tactile
// ==================================================

void handleHomeTouch(int x, int y) {

    for (int i = 0; i < BUTTON_COUNT; i++) {

        if (buttons[i].contains(x, y)) {

            Serial.printf(
                "Opening: %s\n",
                buttons[i].title
            );

            showScreen(buttons[i].target);

            return;
        }
    }
}