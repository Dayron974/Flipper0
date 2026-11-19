// Flipper0 - partie Apprenti A (CC1101 433 MHz sur M5Stack Tab5)
//   A03 : verification du raccordement + pilote CC1101 (RadioLib 8.0.0) adapte a la Tab5
//   A04 : scan RSSI 433,05-434,79 MHz avec 3 profils de reglage, mesures repetees
//   A05 : capture RAW (impulsions OOK) et paquets FSK, horodates, sortie CSV
//
// Commandes dans le moniteur serie (115200 bauds) :
//   t = test raccordement    s = scan RSSI    r = capture RAW
//   p = capture paquets      x = stop         h = aide

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "esp_timer.h"

// ===================== Broches Tab5 =====================
#define PIN_SCK    5    // M5-Bus broche 11 <- SCK du CC1101
#define PIN_MISO   19   // M5-Bus broche 9  <- MISO (GDO1) du CC1101
#define PIN_MOSI   18   // M5-Bus broche 7  <- MOSI du CC1101
#define PIN_CSN    2    // M5-Bus broche 21 <- CSN du CC1101
#define PIN_GDO0   3    // M5-Bus broche 19 <- GDO0 du CC1101
// GDO2 du CC1101 : non utilise (laisser en l'air)
// VCC du CC1101 : 3,3 V UNIQUEMENT (M5-Bus broche 12) - GND : M5-Bus broche 1

// ===================== Reglages radio =====================
#define FREQ_CENTRE_HZ   433920000UL
#define DEBIT_BPS        4800
#define DEVIATION_HZ     5000
#define BANDE_RX_HZ      203000      // RAW : large pour la derive des telecommandes
#define PUISSANCE_DBM    10

// ===================== Reglages scan (A04) =====================
#define SCAN_DEBUT_HZ    433050000UL
#define SCAN_FIN_HZ      434790000UL
#define SCAN_PAS_HZ      25000UL
#define SCAN_ECHANTILLONS 8          // lectures RSSI par frequence
#define SCAN_PASSES      5           // balayages repetes par profil
const uint32_t PROFILS_BW[] = { 58000, 135000, 325000 };
const uint8_t NB_PROFILS = 3;

// ===================== Reglages capture RAW (A05) =====================
#define SEUIL_RSSI_DBM   -80.0       // au-dessus : un signal est present
#define FIN_TRAME_MS     10          // signal absent depuis 10 ms : fin de trame
#define MAX_FRONTS       2048
#define MIN_FRONTS       16          // en dessous : bruit, trame ignoree

CC1101 radio = new Module(PIN_CSN, PIN_GDO0, RADIOLIB_NC, RADIOLIB_NC, SPI);

enum Mode { ATTENTE, RAW, PAQUETS };
Mode mode = ATTENTE;

// ----- RAW : rempli par l'interruption sur GDO0 -----
volatile int32_t  fronts[MAX_FRONTS];     // duree signee en us : + = niveau haut, - = niveau bas
volatile uint16_t nbFronts = 0;
volatile uint32_t nbDebordements = 0;
volatile uint32_t dernierFrontUs = 0;
volatile bool     enregistrement = false;
int64_t debutTrameUs = 0;
float   rssiMaxTrame = -130.0;
uint32_t dernierSignalMs = 0;
uint32_t numeroTrame = 0;

// ----- PAQUETS : drapeau leve par l'interruption -----
volatile bool paquetRecu = false;

// ============================================================
//                      Outils
// ============================================================
int64_t tempsUs() { return esp_timer_get_time(); }

void afficherCode(const char* etape, int code)
{
  Serial.printf("# %-22s : %s (code %d)\n", etape, code == RADIOLIB_ERR_NONE ? "OK" : "ERREUR", code);
}

int configurerBase(uint32_t bandeRxHz)
{
  ConfigFSK_t cfg;
  cfg.frequency = FREQ_CENTRE_HZ;
  cfg.bitRate = DEBIT_BPS;
  cfg.frequencyDeviation = DEVIATION_HZ;
  cfg.receiverBandwidth = bandeRxHz;
  cfg.power = PUISSANCE_DBM;
  int code = radio.begin(cfg);
  if (code != RADIOLIB_ERR_NONE) return code;
  return radio.setPromiscuousMode(false);
}

void arreter()
{
  detachInterrupt(digitalPinToInterrupt(PIN_GDO0));
  radio.clearGdo0Action();
  enregistrement = false;
  radio.standby();
  mode = ATTENTE;
}

void aide()
{
  Serial.println();
  Serial.println("# ===== Flipper0 - CC1101 433 MHz =====");
  Serial.println("# t : test du raccordement (A03)");
  Serial.println("# s : scan RSSI 433 MHz, 3 profils x 5 passes (A04)");
  Serial.println("# r : capture RAW horodatee (A05)");
  Serial.println("# p : capture paquets FSK horodatee (A05)");
  Serial.println("# x : stop");
  Serial.println("# h : cette aide");
}

// ============================================================
//            A03 - Verification du raccordement
// ============================================================
volatile uint32_t compteurFrontsTest = 0;
void IRAM_ATTR compterFront() { compteurFrontsTest++; }

void testRaccordement()
{
  arreter();
  Serial.println("\n# ===== A03 : test du raccordement =====");
  bool toutOk = true;

  // 1) SPI : le pilote lit le registre VERSION et configure la puce
  int code = configurerBase(BANDE_RX_HZ);
  afficherCode("1. begin (SPI + config)", code);
  if (code == RADIOLIB_ERR_CHIP_NOT_FOUND)
  {
    Serial.println("#    -> puce introuvable : verifier SCK, MISO, MOSI, CSN, VCC 3,3 V, GND");
    return;
  }
  if (code != RADIOLIB_ERR_NONE) return;

  // 2) Version de la puce
  int version = radio.getChipVersion();
  Serial.printf("# 2. registre VERSION    : 0x%02X ", version);
  if (version == RADIOLIB_CC1101_VERSION_CURRENT) Serial.println("(CC1101 officiel)");
  else if (version == RADIOLIB_CC1101_VERSION_LEGACY) Serial.println("(CC1101 ancienne revision)");
  else Serial.println("(clone compatible)");

  // 3) Ecriture + relecture : SPIsetRegValue relit le registre et compare
  int c1 = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_SYNC1, 0x55);
  int c2 = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_SYNC1, 0xAA);
  afficherCode("3. ecriture/relecture", (c1 != RADIOLIB_ERR_NONE) ? c1 : c2);
  if (c1 != RADIOLIB_ERR_NONE || c2 != RADIOLIB_ERR_NONE)
  {
    Serial.println("#    -> lecture OK mais ecriture KO : verifier MOSI");
    toutOk = false;
  }
  configurerBase(BANDE_RX_HZ);   // remet le mot de synchro d'origine

  // 4) Fil GDO0 : en mode RAW OOK, GDO0 recopie le bruit -> il doit bouger
  radio.setOOK(true);
  radio.receiveDirect();
  compteurFrontsTest = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), compterFront, CHANGE);
  delay(500);
  detachInterrupt(digitalPinToInterrupt(PIN_GDO0));
  Serial.printf("# 4. fronts sur GDO0     : %lu en 500 ms ", (unsigned long)compteurFrontsTest);
  if (compteurFrontsTest > 0) Serial.println("-> fil GDO0 OK");
  else { Serial.println("-> aucun : verifier le fil GDO0"); toutOk = false; }

  // 5) RSSI : une valeur entre -120 et -40 dBm sans emetteur proche = chaine RX vivante
  float rssi = radio.getRSSI();
  Serial.printf("# 5. RSSI a vide         : %.0f dBm %s\n", rssi,
                (rssi > -125 && rssi < -40) ? "-> chaine de reception OK" : "-> valeur anormale");

  radio.standby();
  Serial.println(toutOk ? "# RESULTAT A03 : raccordement OK" : "# RESULTAT A03 : probleme, voir ci-dessus");
}

// ============================================================
//            A04 - Scan RSSI et comparaison des reglages
// ============================================================
float mesurerRSSI(uint32_t freqHz, float* rssiMax)
{
  radio.setFrequency(freqHz);
  radio.receiveDirect();          // relance la reception (calibration auto du synthetiseur)
  delay(2);                       // temps de calibration + stabilisation de l'AGC
  float somme = 0;
  *rssiMax = -130;
  for (int i = 0; i < SCAN_ECHANTILLONS; i++)
  {
    float r = radio.getRSSI();
    somme += r;
    if (r > *rssiMax) *rssiMax = r;
    delayMicroseconds(250);
  }
  return somme / SCAN_ECHANTILLONS;
}

void scanRSSI()
{
  arreter();
  Serial.println("\n# ===== A04 : scan RSSI 433,05 - 434,79 MHz =====");
  Serial.println("SCAN;profil;bande_rx_hz;passe;freq_hz;rssi_moy_dbm;rssi_max_dbm");

  for (uint8_t p = 0; p < NB_PROFILS; p++)
  {
    int code = configurerBase(PROFILS_BW[p]);
    if (code != RADIOLIB_ERR_NONE) { afficherCode("configuration profil", code); return; }
    radio.setOOK(true);

    float plancherTotal = 0, picMax = -130;
    uint32_t freqPic = 0, nbMesures = 0;
    int64_t t0 = tempsUs();

    for (uint8_t passe = 1; passe <= SCAN_PASSES; passe++)
    {
      for (uint32_t f = SCAN_DEBUT_HZ; f <= SCAN_FIN_HZ; f += SCAN_PAS_HZ)
      {
        float rMax;
        float rMoy = mesurerRSSI(f, &rMax);
        Serial.printf("SCAN;P%u;%lu;%u;%lu;%.1f;%.1f\n", p + 1, (unsigned long)PROFILS_BW[p], passe,
                      (unsigned long)f, rMoy, rMax);
        plancherTotal += rMoy;
        nbMesures++;
        if (rMax > picMax) { picMax = rMax; freqPic = f; }
        if (Serial.available() && Serial.peek() == 'x') { arreter(); return; }
      }
    }

    float dureeBalayage = (tempsUs() - t0) / 1000.0 / SCAN_PASSES;
    Serial.printf("# Profil P%u (bande RX %lu Hz) : niveau moyen %.1f dBm, pic %.1f dBm a %lu Hz, %.0f ms par balayage\n",
                  p + 1, (unsigned long)PROFILS_BW[p], plancherTotal / nbMesures, picMax,
                  (unsigned long)freqPic, dureeBalayage);
  }
  radio.standby();
  Serial.println("# Scan termine");
}

// ============================================================
//            A05 - Capture RAW horodatee
// ============================================================
void IRAM_ATTR surFrontGDO0()
{
  uint32_t maintenant = micros();
  if (!enregistrement) { dernierFrontUs = maintenant; return; }

  int32_t duree = (int32_t)(maintenant - dernierFrontUs);
  dernierFrontUs = maintenant;
  if (digitalRead(PIN_GDO0) == HIGH) duree = -duree;   // on vient de passer a 1 : l'impulsion finie etait basse

  if (nbFronts < MAX_FRONTS) fronts[nbFronts++] = duree;
  else nbDebordements++;
}

void demarrerRAW()
{
  arreter();
  int code = configurerBase(BANDE_RX_HZ);
  if (code == RADIOLIB_ERR_NONE) code = radio.setOOK(true);
  if (code == RADIOLIB_ERR_NONE) code = radio.receiveDirect();
  afficherCode("demarrage RAW", code);
  if (code != RADIOLIB_ERR_NONE) return;

  nbFronts = 0;
  nbDebordements = 0;
  enregistrement = false;
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), surFrontGDO0, CHANGE);
  mode = RAW;
  Serial.println("RAW;num;t_us;rssi_max_dbm;nb_fronts;debordements;durees_us(+haut/-bas)");
}

void envoyerTrameRAW()
{
  static int32_t copie[MAX_FRONTS];
  noInterrupts();
  uint16_t n = nbFronts;
  uint32_t perdus = nbDebordements;
  for (uint16_t i = 0; i < n; i++) copie[i] = fronts[i];
  nbFronts = 0;
  nbDebordements = 0;
  interrupts();

  if (n < MIN_FRONTS) return;
  numeroTrame++;
  Serial.printf("RAW;%lu;%lld;%.0f;%u;%lu;", (unsigned long)numeroTrame, (long long)debutTrameUs, rssiMaxTrame, n,
                (unsigned long)perdus);
  for (uint16_t i = 0; i < n; i++) Serial.printf("%ld ", (long)copie[i]);
  Serial.println();
}

void gererRAW()
{
  static uint32_t derniereLecture = 0;
  if (millis() - derniereLecture < 1) return;
  derniereLecture = millis();

  float rssi = radio.getRSSI();

  if (rssi >= SEUIL_RSSI_DBM)
  {
    if (!enregistrement)
    {
      noInterrupts();
      nbFronts = 0;
      nbDebordements = 0;
      dernierFrontUs = micros();
      enregistrement = true;
      interrupts();
      debutTrameUs = tempsUs();
      rssiMaxTrame = rssi;
    }
    if (rssi > rssiMaxTrame) rssiMaxTrame = rssi;
    dernierSignalMs = millis();
  }
  else if (enregistrement && millis() - dernierSignalMs > FIN_TRAME_MS)
  {
    enregistrement = false;
    envoyerTrameRAW();
  }
}

// ============================================================
//            A05 - Capture paquets FSK horodatee
// ============================================================
void IRAM_ATTR surPaquet() { paquetRecu = true; }

void demarrerPaquets()
{
  arreter();
  int code = configurerBase(135000);
  if (code == RADIOLIB_ERR_NONE) code = radio.packetMode();
  if (code == RADIOLIB_ERR_NONE)
  {
    radio.setGdo0Action(surPaquet, RISING);
    paquetRecu = false;
    code = radio.startReceive();
  }
  afficherCode("demarrage paquets", code);
  if (code != RADIOLIB_ERR_NONE) return;
  mode = PAQUETS;
  Serial.println("PKT;num;t_us;rssi_dbm;lqi;longueur;crc_ok;donnees_hex");
}

void gererPaquets()
{
  if (!paquetRecu) return;
  paquetRecu = false;
  int64_t t = tempsUs();

  uint8_t donnees[64];
  size_t longueur = radio.getPacketLength();
  if (longueur > sizeof(donnees)) longueur = sizeof(donnees);
  int code = radio.readData(donnees, longueur);

  if (code == RADIOLIB_ERR_NONE || code == RADIOLIB_ERR_CRC_MISMATCH)
  {
    numeroTrame++;
    Serial.printf("PKT;%lu;%lld;%.1f;%u;%u;%d;", (unsigned long)numeroTrame, (long long)t, radio.getRSSI(),
                  radio.getLQI(), (unsigned)longueur, code == RADIOLIB_ERR_NONE ? 1 : 0);
    for (size_t i = 0; i < longueur; i++) Serial.printf("%02X", donnees[i]);
    Serial.println();
  }
  else afficherCode("lecture paquet", code);

  radio.startReceive();
}

// ============================================================
//                      setup / loop
// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(2000);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  pinMode(PIN_GDO0, INPUT);
  aide();
}

void loop()
{
  if (Serial.available())
  {
    char c = Serial.read();
    if (c == 't') testRaccordement();
    else if (c == 's') scanRSSI();
    else if (c == 'r') demarrerRAW();
    else if (c == 'p') demarrerPaquets();
    else if (c == 'x') { arreter(); Serial.println("# Stop"); }
    else if (c == 'h') aide();
  }

  if (mode == RAW) gererRAW();
  else if (mode == PAQUETS) gererPaquets();
}
