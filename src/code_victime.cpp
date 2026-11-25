
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// Broches identiques au module de reception
#define PIN_SCK   5
#define PIN_MISO  19
#define PIN_MOSI  18
#define PIN_CSN   2
#define PIN_GDO0  3

// Parametres radio
#define FREQ_HZ       433920000UL
#define DEBIT_BPS     4800
#define DEVIATION_HZ  5000
#define BANDE_RX_HZ   135000
#define PUISSANCE_DBM 0

CC1101 radio(new Module(
    PIN_CSN, PIN_GDO0,
    RADIOLIB_NC, RADIOLIB_NC, SPI
));

bool radioOK = false;
unsigned long compteur = 0;

void aide() {
    Serial.println("\n=== MODULE VICTIME ===");
    Serial.println("t : tester le CC1101");
    Serial.println("f : envoyer une trame FSK");
    Serial.println("x : standby");
    Serial.println("h : aide");
}

void initialiserRadio() {
    ConfigFSK_t cfg;

    cfg.frequency = FREQ_HZ;
    cfg.bitRate = DEBIT_BPS;
    cfg.frequencyDeviation = DEVIATION_HZ;
    cfg.receiverBandwidth = BANDE_RX_HZ;
    cfg.power = PUISSANCE_DBM;

    int state = radio.begin(cfg);

    radioOK = (state == RADIOLIB_ERR_NONE);

    if (radioOK) {
        Serial.println("CC1101 initialise : OK");
        Serial.printf("Version : 0x%02X\n",
                      radio.getChipVersion());
    } else {
        Serial.printf("Erreur CC1101 : %d\n", state);
    }
}

void emettreFSK() {
    if (!radioOK) {
        Serial.println("Radio non initialisee");
        return;
    }

    char message[48];

    snprintf(message, sizeof(message),
             "VICTIME_TEST_%lu", ++compteur);

    int state = radio.transmit(
        (uint8_t*)message, strlen(message)
    );

    if (state == RADIOLIB_ERR_NONE) {
        Serial.print("Trame emise : ");
        Serial.println(message);
    } else {
        Serial.printf("Erreur emission : %d\n", state);
    }

    radio.standby();
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    SPI.begin(PIN_SCK, PIN_MISO,
              PIN_MOSI, PIN_CSN);

    initialiserRadio();
    aide();
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();

        switch (c) {
            case 't':
                initialiserRadio();
                break;

            case 'f':
                emettreFSK();
                break;

            case 'x':
                if (radioOK) radio.standby();
                Serial.println("Emission arretee");
                break;

            case 'h':
                aide();
                break;
        }
    }
}
