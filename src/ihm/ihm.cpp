#include <M5Unified.h>
#include "ihm.h"

// ==================================================
// Etat actuel de l'application
// ==================================================

static Screen currentScreen = Screen::HOME;

// ==================================================
// Dimensions du bouton Retour
// ==================================================

constexpr int BACK_X = 40;
constexpr int BACK_Y = 25;
constexpr int BACK_W = 150;
constexpr int BACK_H = 70;

// ==================================================
// Elements graphiques communs aux ecrans
// ==================================================

void drawScreenFrame(
    const char* title,
    const char* description
) {

    M5.Display.fillScreen(COLOR_BG);

    // Bouton Retour
    M5.Display.fillRoundRect(
        BACK_X,
        BACK_Y,
        BACK_W,
        BACK_H,
        12,
        COLOR_CARD
    );

    M5.Display.drawRoundRect(
        BACK_X,
        BACK_Y,
        BACK_W,
        BACK_H,
        12,
        COLOR_BORDER
    );

    M5.Display.setTextColor(COLOR_TEXT);
    M5.Display.setTextSize(3);

    M5.Display.drawString(
        "< Back",
        BACK_X + 15,
        BACK_Y + 23
    );

    // Titre de l'ecran
    M5.Display.setTextColor(COLOR_ACCENT);
    M5.Display.setTextSize(4);

    M5.Display.drawString(title, 230, 40);

    // Ligne de separation
    M5.Display.drawFastHLine(
        40,
        116,
        M5.Display.width() - 80,
        COLOR_BORDER
    );

    // Description
    M5.Display.setTextColor(COLOR_MUTED);
    M5.Display.setTextSize(2);

    M5.Display.drawString(description, 40, 150);
}

// ==================================================
// Changement d'ecran
// ==================================================

void showScreen(Screen screen) {

    currentScreen = screen;

    switch (currentScreen) {

        case Screen::HOME:
            drawHome();
            break;

        case Screen::SCANNER:
            drawScanner();
            break;

        case Screen::RAW_VIEWER:
            drawRawViewer();
            break;

        case Screen::FSK_VIEWER:
            drawFskViewer();
            break;

        case Screen::ANALYZER:
            drawAnalyzer();
            break;

        case Screen::SAVED_SIGNALS:
            drawSavedSignals();
            break;

        case Screen::SETTINGS:
            drawSettings();
            break;
    }
}

// ==================================================
// Detection du bouton Retour
// ==================================================

static bool backPressed(int x, int y) {

    return x >= BACK_X &&
           x < BACK_X + BACK_W &&
           y >= BACK_Y &&
           y < BACK_Y + BACK_H;
}

// ==================================================
// Initialisation de l'IHM
// ==================================================

void initIHM() {

    auto cfg = M5.config();
    M5.begin(cfg);

    if (M5.Display.height() > M5.Display.width()) {
        M5.Display.setRotation(3);
    }

    showScreen(Screen::HOME);

    Serial.println("IHM initialized");
}

// ==================================================
// Actualisation de l'IHM
// ==================================================

void updateIHM() {

    M5.update();

    auto touch = M5.Touch.getDetail();

    if (!touch.wasPressed()) {
        return;
    }

    int x = touch.x;
    int y = touch.y;

    // Gestion du menu principal
    if (currentScreen == Screen::HOME) {

        handleHomeTouch(x, y);
        return;
    }

    // Gestion du bouton Retour
    if (backPressed(x, y)) {

        showScreen(Screen::HOME);
        return;
    }
}