// =====================================================================
//  Flipper0 - Partie A (CC1101 433 MHz sur M5Stack Tab5)
//    A03 : test du raccordement            -> commande t
//    A04 : scan RSSI, 3 reglages compares  -> commande s
//    A04 : recherche de frequence en direct -> commande f
//    A05 : capture RAW horodatee           -> commande r
//    A05 : capture paquets FSK horodatee   -> commande p
//    B03 : tests du decodeur sans radio    -> commande d
//  Pendant une capture ou un scan : x = stop
// =====================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <RadioLib.h>
#include "esp_timer.h"
#include "decoder.h"

// ----------------------- Cablage (M-BUS de la Tab5) -----------------------
#define PIN_SCK    5     // broche 11
#define PIN_MISO   19    // broche 9
#define PIN_MOSI   18    // broche 7
#define PIN_CSN    2     // broche 21
#define PIN_GDO0   3     // broche 19
// VCC -> 3V3 (broche 12)   GND -> GND (broche 1)   GDO2 non branche

// ----------------------- Reglages radio -----------------------
#define FREQUENCE_HZ   433920000UL   // frequence par defaut (telecommandes classiques)
#define DEBIT_BPS      4800          // debit des paquets FSK
#define DEVIATION_HZ   5000          // ecart de frequence FSK entre 0 et 1
#define BANDE_RAW_HZ   203000        // large : tolere la derive des telecommandes
#define BANDE_PKT_HZ   135000
#define PUISSANCE_DBM  10

// ----------------------- Reglages scan (A04) -----------------------
#define SCAN_DEBUT_HZ  433050000UL
#define SCAN_FIN_HZ    434790000UL
#define SCAN_PAS_HZ    25000UL
#define SCAN_LECTURES  8             // lectures RSSI par frequence
#define SCAN_PASSES    5             // balayages repetes par reglage
const uint32_t BANDES_SCAN[3] = { 58000, 135000, 325000 };

// ----------------------- Recherche de frequence (f) -----------------------
#define RECH_DEBUT_HZ  430000000UL   // plus large que la bande legale : on cherche ou emet l'appareil
#define RECH_FIN_HZ    440000000UL
#define RECH_PAS_HZ    50000UL       // 201 frequences -> environ 2 balayages par seconde
#define RECH_BANDE_HZ  58000         // bande etroite : meilleure precision en frequence
#define RECH_MARGE_DB  20            // un signal doit depasser le bruit de 20 dB pour etre retenu

// ----------------------- Reglages capture RAW (A05) -----------------------
#define MARGE_SEUIL_DB 10            // seuil = bruit mesure + 10 dB
#define FIN_TRAME_MS   30            // 30 ms sans signal = fin de trame (> synchro EV1527 ~11 ms)
#define MAX_FRONTS     2048          // taille du buffer
#define MARGE_FRONTS   64            // on envoie la trame avant que le buffer deborde
#define MIN_FRONTS     16            // moins d'impulsions = bruit

// ----------------------- Capture paquets (A05) -----------------------
#define LONGUEUR_MAX   61            // FIFO 64 o - 1 o de longueur - 2 o d'etat : au-dela = faux paquet

// Frequence utilisee par r et p : 433,92 MHz au demarrage, remplacee par celle trouvee avec f
uint32_t frequenceCible = FREQUENCE_HZ;

// Pilote CC1101 de RadioLib, adapte : rend accessible l'ecriture de registre
class CC1101Flipper : public CC1101
{
public:
  CC1101Flipper(Module* m) : CC1101(m) {}
  using CC1101::SPIsetRegValue;
};
CC1101Flipper radio(new Module(PIN_CSN, PIN_GDO0, RADIOLIB_NC, RADIOLIB_NC, SPI));

// =====================================================================
//                              Outils
// =====================================================================
int64_t tempsUs() { return esp_timer_get_time(); }

void afficher(const char* etape, int code)
{
  Serial.printf("# %-24s : %s (code %d)\n", etape, code == RADIOLIB_ERR_NONE ? "OK" : "ERREUR", code);
}

// x tape dans le moniteur = arreter la mesure en cours
bool stopDemande()
{
  return Serial.available() && Serial.read() == 'x';
}

// Demarre le CC1101 avec nos reglages
int demarrerRadio(uint32_t bandeRxHz)
{
  ConfigFSK_t cfg;
  cfg.frequency = frequenceCible;
  cfg.bitRate = DEBIT_BPS;
  cfg.frequencyDeviation = DEVIATION_HZ;
  cfg.receiverBandwidth = bandeRxHz;
  cfg.power = PUISSANCE_DBM;
  int code = radio.begin(cfg);
  if (code != RADIOLIB_ERR_NONE) return code;
  return radio.setPromiscuousMode(false);
}

// Passe en reception OOK brute : GDO0 recopie le signal demodule
// L'AGC est regle pour l'OOK (valeurs du preset AM650 du Flipper Zero)
int passerEnOOK()
{
  int code = radio.setOOK(true);
  if (code == RADIOLIB_ERR_NONE) code = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_AGCCTRL2, 0x07);
  if (code == RADIOLIB_ERR_NONE) code = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_AGCCTRL1, 0x00);
  if (code == RADIOLIB_ERR_NONE) code = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_AGCCTRL0, 0x91);
  if (code == RADIOLIB_ERR_NONE) code = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_FREND1, 0xB6);
  if (code == RADIOLIB_ERR_NONE) code = radio.receiveDirectAsync();
  return code;
}

// Moyenne de plusieurs lectures RSSI
float moyenneRSSI(int lectures, int pauseUs)
{
  float somme = 0;
  for (int i = 0; i < lectures; i++)
  {
    somme += radio.getRSSI();
    delayMicroseconds(pauseUs);
  }
  return somme / lectures;
}

// =====================================================================
//                    A03 - Test du raccordement
// =====================================================================
volatile uint32_t nbFrontsTest = 0;
void IRAM_ATTR compterFront() { nbFrontsTest = nbFrontsTest + 1; }

void testRaccordement()
{
  Serial.println("\n# ===== A03 : test du raccordement =====");

  // 1) Le pilote lit le registre VERSION et configure la puce
  int code = demarrerRadio(BANDE_RAW_HZ);
  afficher("1. puce detectee", code);
  if (code != RADIOLIB_ERR_NONE)
  {
    Serial.println("#    -> verifier VCC 3,3 V, GND, SCK, MISO, MOSI, CSN");
    return;
  }
  Serial.printf("# 2. registre VERSION      : 0x%02X (0x14 = CC1101 officiel)\n", radio.getChipVersion());

  // 3) Ecriture puis relecture d'un registre : teste le fil MOSI
  int c1 = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_SYNC1, 0x55);
  int c2 = radio.SPIsetRegValue(RADIOLIB_CC1101_REG_SYNC1, 0xAA);
  afficher("3. ecriture/relecture", (c1 != RADIOLIB_ERR_NONE) ? c1 : c2);

  // 4) En OOK brut, GDO0 bouge avec le bruit : teste le fil GDO0
  demarrerRadio(BANDE_RAW_HZ);
  passerEnOOK();
  nbFrontsTest = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), compterFront, CHANGE);
  delay(500);
  detachInterrupt(digitalPinToInterrupt(PIN_GDO0));
  Serial.printf("# 4. fronts sur GDO0       : %lu en 500 ms %s\n", (unsigned long)nbFrontsTest,
                nbFrontsTest > 0 ? "-> fil GDO0 OK" : "-> aucun : verifier le fil GDO0");

  // 5) RSSI a vide : une valeur normale prouve que la reception fonctionne
  float rssi = moyenneRSSI(20, 1000);
  Serial.printf("# 5. RSSI a vide           : %.0f dBm %s\n", rssi,
                (rssi > -125 && rssi < -40) ? "-> reception OK" : "-> valeur anormale");
  radio.standby();
}

// =====================================================================
//              A04 - Scan RSSI et comparaison des reglages
// =====================================================================
void scanRSSI()
{
  Serial.println("\n# ===== A04 : scan RSSI 433,05 - 434,79 MHz =====");
  Serial.println("SCAN;reglage;bande_rx_hz;passe;freq_hz;rssi_moy_dbm;rssi_max_dbm");

  for (int r = 0; r < 3; r++)
  {
    int code = demarrerRadio(BANDES_SCAN[r]);
    if (code != RADIOLIB_ERR_NONE) { afficher("demarrage scan", code); return; }
    radio.setOOK(true);

    float somme = 0, pic = -130;
    uint32_t freqPic = 0, nb = 0;

    for (int passe = 1; passe <= SCAN_PASSES; passe++)
    {
      for (uint32_t f = SCAN_DEBUT_HZ; f <= SCAN_FIN_HZ; f += SCAN_PAS_HZ)
      {
        radio.setFrequency(f);
        radio.receiveDirectAsync();   // relance la reception sur la nouvelle frequence
        delay(2);                     // calibration du synthetiseur + stabilisation de l'AGC

        float total = 0, maxi = -130;
        for (int i = 0; i < SCAN_LECTURES; i++)
        {
          float rssi = radio.getRSSI();
          total += rssi;
          if (rssi > maxi) maxi = rssi;
          delayMicroseconds(250);
        }
        float moy = total / SCAN_LECTURES;

        Serial.printf("SCAN;P%d;%lu;%d;%lu;%.1f;%.1f\n", r + 1, (unsigned long)BANDES_SCAN[r], passe,
                      (unsigned long)f, moy, maxi);
        somme += moy;
        nb++;
        if (maxi > pic) { pic = maxi; freqPic = f; }
        if (stopDemande()) { radio.standby(); Serial.println("# Scan arrete"); return; }
      }
    }
    Serial.printf("# Reglage P%d (bande %lu Hz) : niveau moyen %.1f dBm, pic %.1f dBm a %lu Hz\n",
                  r + 1, (unsigned long)BANDES_SCAN[r], somme / nb, pic, (unsigned long)freqPic);
  }
  radio.standby();
  Serial.println("# Scan termine");
}

// =====================================================================
//        A04 - Recherche de frequence en direct (commande f)
//  Garder le bouton de la telecommande appuye : la frequence qui
//  ressort a chaque balayage est celle de l'emetteur.
// =====================================================================
void rechercheFrequence()
{
  Serial.println("\n# ===== Recherche de frequence 430 - 440 MHz (x pour arreter) =====");
  int code = demarrerRadio(RECH_BANDE_HZ);
  if (code == RADIOLIB_ERR_NONE) code = radio.setOOK(true);
  afficher("demarrage recherche", code);
  if (code != RADIOLIB_ERR_NONE) return;
  Serial.println("# Maintiens le bouton de la telecommande appuye pres de l'antenne");

  float meilleurPic = -130;      // signal le plus fort vu depuis le debut de la recherche

  while (!stopDemande())
  {
    // Un balayage : on garde la frequence la plus forte et le niveau moyen (= bruit)
    float pic = -130, somme = 0;
    uint32_t freqPic = 0, nb = 0;
    for (uint32_t f = RECH_DEBUT_HZ; f <= RECH_FIN_HZ; f += RECH_PAS_HZ)
    {
      radio.setFrequency(f);
      radio.receiveDirectAsync();
      delay(2);
      float rssi = radio.getRSSI();
      somme += rssi;
      nb++;
      if (rssi > pic) { pic = rssi; freqPic = f; }
    }
    float bruit = somme / nb;
    Serial.printf("# plus fort : %.3f MHz a %.0f dBm (bruit %.0f dBm)", freqPic / 1e6, pic, bruit);

    // Un vrai signal depasse nettement le bruit : on memorise sa frequence
    if (pic - bruit >= RECH_MARGE_DB && pic > meilleurPic)
    {
      meilleurPic = pic;
      frequenceCible = freqPic;
      Serial.print("  <- signal retenu");
    }
    Serial.println();
  }
  radio.standby();
  Serial.printf("# Recherche arretee. Frequence utilisee par r et p : %.3f MHz\n", frequenceCible / 1e6);
}

// =====================================================================
//                     A05 - Capture RAW horodatee
// =====================================================================
// L'interruption range la duree de chaque impulsion : + = haut, - = bas
volatile int32_t  fronts[MAX_FRONTS];
volatile uint16_t nbFronts = 0;
volatile uint32_t nbPerdus = 0;       // impulsions perdues si le buffer est plein
volatile uint32_t dernierFrontUs = 0;
volatile bool     enregistre = false;

void IRAM_ATTR surFront()
{
  uint32_t maintenant = micros();
  int32_t duree = (int32_t)(maintenant - dernierFrontUs);
  dernierFrontUs = maintenant;
  if (!enregistre) return;

  if (digitalRead(PIN_GDO0) == HIGH) duree = -duree;   // on vient de passer a 1 : l'impulsion finie etait basse
  if (nbFronts < MAX_FRONTS)
  {
    fronts[nbFronts] = duree;
    nbFronts = nbFronts + 1;
  }
  else nbPerdus = nbPerdus + 1;
}

// Copie le buffer, affiche la trame RAW, puis la donne au decodeur B03
void envoyerTrame(uint32_t numero, int64_t debutUs, float rssiMax)
{
  static int32_t copie[MAX_FRONTS];
  noInterrupts();
  enregistre = false;
  uint16_t n = nbFronts;
  uint32_t perdus = nbPerdus;
  for (uint16_t i = 0; i < n; i++) copie[i] = fronts[i];
  nbFronts = 0;
  nbPerdus = 0;
  interrupts();

  if (n < MIN_FRONTS) return;

  Serial.printf("RAW;%lu;%lld;%.0f;%u;%lu;", (unsigned long)numero, (long long)debutUs, rssiMax, n,
                (unsigned long)perdus);
  for (uint16_t i = 0; i < n; i++) Serial.printf("%ld ", (long)copie[i]);
  Serial.println();

  analyserRAW(copie, n, perdus, numero);
}

void captureRAW()
{
  Serial.println("\n# ===== A05 : capture RAW (x pour arreter) =====");
  Serial.printf("# Frequence : %.3f MHz (tape f pour chercher celle de ta telecommande)\n", frequenceCible / 1e6);
  int code = demarrerRadio(BANDE_RAW_HZ);
  if (code == RADIOLIB_ERR_NONE) code = passerEnOOK();
  afficher("reception OOK", code);
  if (code != RADIOLIB_ERR_NONE) return;

  // Le seuil s'adapte au bruit ambiant
  delay(50);
  float bruit = moyenneRSSI(100, 2000);
  float seuil = bruit + MARGE_SEUIL_DB;
  Serial.printf("# Bruit mesure : %.0f dBm -> seuil de declenchement : %.0f dBm\n", bruit, seuil);
  Serial.println("RAW;num;t_us;rssi_max_dbm;nb_fronts;perdus;durees_us(+haut/-bas)");

  nbFronts = 0;
  nbPerdus = 0;
  enregistre = false;
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), surFront, CHANGE);

  uint32_t numero = 0;
  bool trameEnCours = false;
  int64_t debutUs = 0;
  float rssiMax = -130;
  uint32_t dernierSignalMs = 0;
  uint32_t dernierAffichageMs = 0;

  while (!stopDemande())
  {
    float rssi = radio.getRSSI();

    // Signal present : on demarre (ou on continue) l'enregistrement
    if (rssi >= seuil)
    {
      if (!trameEnCours)
      {
        noInterrupts();
        nbFronts = 0;
        nbPerdus = 0;
        dernierFrontUs = micros();
        enregistre = true;
        interrupts();
        trameEnCours = true;
        debutUs = tempsUs();
        rssiMax = rssi;
      }
      if (rssi > rssiMax) rssiMax = rssi;
      dernierSignalMs = millis();
    }

    // Fin de trame : silence assez long, ou buffer presque plein
    bool silence = trameEnCours && (millis() - dernierSignalMs > FIN_TRAME_MS);
    bool plein = trameEnCours && (nbFronts >= MAX_FRONTS - MARGE_FRONTS);
    if (silence || plein)
    {
      numero++;
      envoyerTrame(numero, debutUs, rssiMax);
      trameEnCours = false;
    }

    // Toutes les secondes sans trame : on montre qu'on ecoute
    if (!trameEnCours && millis() - dernierAffichageMs >= 1000)
    {
      Serial.printf("# ecoute... RSSI = %.0f dBm (seuil %.0f dBm)\n", rssi, seuil);
      dernierAffichageMs = millis();
    }
    delay(1);
  }

  detachInterrupt(digitalPinToInterrupt(PIN_GDO0));
  enregistre = false;
  radio.standby();
  Serial.println("# Capture RAW arretee");
}

// =====================================================================
//                  A05 - Capture paquets FSK horodatee
// =====================================================================
volatile bool paquetRecu = false;
void IRAM_ATTR surPaquet() { paquetRecu = true; }

void capturePaquets()
{
  Serial.println("\n# ===== A05 : capture paquets FSK (x pour arreter) =====");
  Serial.printf("# Frequence : %.3f MHz\n", frequenceCible / 1e6);
  int code = demarrerRadio(BANDE_PKT_HZ);
  if (code == RADIOLIB_ERR_NONE) code = radio.packetMode();
  if (code == RADIOLIB_ERR_NONE)
  {
    radio.setGdo0Action(surPaquet, RISING);
    paquetRecu = false;
    code = radio.startReceive();
  }
  afficher("reception paquets", code);
  if (code != RADIOLIB_ERR_NONE) return;
  Serial.println("PKT;num;t_us;rssi_dbm;lqi;longueur;crc_ok;donnees_hex");

  uint32_t numero = 0;
  uint32_t fauxPaquets = 0;
  uint32_t dernierAffichageMs = 0;

  while (!stopDemande())
  {
    if (paquetRecu)
    {
      paquetRecu = false;
      int64_t t = tempsUs();

      // Longueur impossible = le "paquet" vient du bruit : on le jette
      size_t longueur = radio.getPacketLength();
      if (longueur == 0 || longueur > LONGUEUR_MAX)
      {
        fauxPaquets++;
        radio.startReceive();
        continue;
      }

      uint8_t donnees[LONGUEUR_MAX];
      bool tronque = false;
      code = radio.readData(donnees, longueur);
      bool crcOk = (code == RADIOLIB_ERR_NONE);

      if (crcOk || code == RADIOLIB_ERR_CRC_MISMATCH)
      {
        numero++;
        Serial.printf("PKT;%lu;%lld;%.1f;%u;%u;%d;", (unsigned long)numero, (long long)t, radio.getRSSI(),
                      radio.getLQI(), (unsigned)longueur, crcOk ? 1 : 0);
        for (size_t i = 0; i < longueur; i++) Serial.printf("%02X", donnees[i]);
        Serial.println();
        analyserFSK(donnees, longueur, crcOk, tronque, numero);
      }
      else afficher("lecture paquet", code);

      radio.startReceive();
    }

    if (millis() - dernierAffichageMs >= 1000)
    {
      Serial.printf("# ecoute des paquets FSK... (%lu faux paquets ignores)\n", (unsigned long)fauxPaquets);
      dernierAffichageMs = millis();
    }
    delay(1);
  }

  radio.clearGdo0Action();
  radio.standby();
  Serial.println("# Capture paquets arretee");
}

// =====================================================================
//                          setup / loop
// =====================================================================
void aide()
{
  Serial.println("\n# ===== Flipper0 - CC1101 433 MHz =====");
  Serial.println("# t : test du raccordement (A03)");
  Serial.println("# s : scan RSSI (A04)");
  Serial.println("# f : recherche de la frequence d'une telecommande (A04)");
  Serial.println("# r : capture RAW (A05)");
  Serial.println("# p : capture paquets FSK (A05)");
  Serial.println("# d : tests du decodeur sans radio (B03)");
  Serial.println("# x : arreter une mesure en cours");
}

void setup()
{
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);                  // demarre la Tab5 et le port serie
  M5.Power.setExtOutput(true);    // alimente les ports d'extension
  delay(2000);                    // laisse le CC1101 demarrer
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  pinMode(PIN_GDO0, INPUT);
  aide();
}

void loop()
{
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 't') testRaccordement();
  else if (c == 's') scanRSSI();
  else if (c == 'f') rechercheFrequence();
  else if (c == 'r') captureRAW();
  else if (c == 'p') capturePaquets();
  else if (c == 'd') testDecodeur();
  else if (c == 'h') aide();
}