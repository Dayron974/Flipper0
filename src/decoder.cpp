#include "decoder.h"
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>

// Profil EV1527 (PWM) : 0 = +T,-3T ; 1 = +3T,-T ; synchro = +T,-31T ; 24 bits par message
#define NB_BITS_ATTENDUS  24
#define TOLERANCE         0.25     // +/-25 % sur chaque duree
#define PARASITE_US       80       // impulsion plus courte = parasite, fusionnee avec ses voisines
#define DUREE_MIN_US      100      // plage utilisee pour estimer T
#define DUREE_MAX_US      2500

static bool proche(double v, double cible)
{
  return std::fabs(v - cible) <= TOLERANCE * cible;
}

// ------------------------------------------------------------------
// 1) Nettoyage de la capture
//    - la 1re duree est tronquee (la capture demarre au milieu d'une impulsion) : on l'ignore
//    - on commence a la premiere impulsion haute
//    - les parasites tres courts sont fusionnes avec l'impulsion precedente
// ------------------------------------------------------------------
static std::vector<int32_t> nettoyer(const int32_t* d, size_t n)
{
  std::vector<int32_t> propre;
  bool fusion = false;

  for (size_t i = 1; i < n; i++)
  {
    int32_t x = d[i];
    int32_t a = std::abs(x);

    if (propre.empty())
    {
      if (x > 0 && a >= PARASITE_US) propre.push_back(x);
      continue;
    }
    if (a < PARASITE_US)
    {
      propre.back() += (propre.back() > 0) ? a : -a;
      fusion = true;
      continue;
    }
    if (fusion && ((x > 0) == (propre.back() > 0)))
    {
      propre.back() += x;
      fusion = false;
      continue;
    }
    fusion = false;
    propre.push_back(x);
  }
  return propre;
}

// ------------------------------------------------------------------
// 2) Estimation de T : en PWM, moitie des durees ~T, moitie ~3T
//    -> apres tri, le premier quart tombe dans le groupe T
// ------------------------------------------------------------------
static double estimerT(const std::vector<int32_t>& v)
{
  std::vector<int32_t> durees;
  for (int32_t x : v)
  {
    int32_t a = std::abs(x);
    if (a >= DUREE_MIN_US && a <= DUREE_MAX_US) durees.push_back(a);
  }
  if (durees.size() < 8) return 0;
  std::sort(durees.begin(), durees.end());
  return durees[durees.size() / 4];
}

static bool estSynchro(const std::vector<int32_t>& v, size_t i, double t)
{
  return i + 1 < v.size() && v[i] > 0 && v[i + 1] < 0 && proche(v[i], t) && proche(-v[i + 1], 31 * t);
}

// ------------------------------------------------------------------
// 3) Decodage d'un segment (entre deux synchros) par paires (haut, bas)
//    La derniere impulsion haute peut ne pas avoir son bas (fin de capture) :
//    on decide alors avec le haut seul.
// ------------------------------------------------------------------
static std::string decoderSegment(const std::vector<int32_t>& v, size_t a, size_t b, double t)
{
  std::string bits;
  for (size_t i = a; i < b; i += 2)
  {
    int32_t h = v[i];
    bool basPresent = (i + 1 < b);
    int32_t l = basPresent ? -v[i + 1] : 0;

    if (h <= 0) { bits += '?'; continue; }

    if (basPresent)
    {
      if (proche(h, t) && proche(l, 3 * t)) bits += '0';
      else if (proche(h, 3 * t) && proche(l, t)) bits += '1';
      else bits += '?';
    }
    else
    {
      if (proche(h, t)) bits += '0';
      else if (proche(h, 3 * t)) bits += '1';
      else bits += '?';
    }
  }
  return bits;
}

static unsigned long bitsVersValeur(const std::string& bits)
{
  unsigned long v = 0;
  for (char c : bits) v = (v << 1) | (c == '1' ? 1 : 0);
  return v;
}

// ------------------------------------------------------------------
// Fonction principale : une capture RAW -> messages decodes
// ------------------------------------------------------------------
void analyserRAW(const int32_t* d, size_t n, uint32_t perdus, uint32_t id)
{
  if (!d || n < 16 || n > 2048)
  {
    Serial.printf("DEC_RAW;%lu;CAPTURE_INEXPLOITABLE;taille\n", (unsigned long)id);
    return;
  }
  if (perdus)
  {
    Serial.printf("DEC_RAW;%lu;CAPTURE_INEXPLOITABLE;debordement\n", (unsigned long)id);
    return;
  }

  std::vector<int32_t> v = nettoyer(d, n);
  double t = estimerT(v);
  if (t <= 0)
  {
    Serial.printf("DEC_RAW;%lu;DUREES_INSUFFISANTES\n", (unsigned long)id);
    return;
  }
  Serial.printf("# B03 capture %lu : T estime = %.1f us\n", (unsigned long)id, t);

  // Reperer les synchros
  std::vector<size_t> debuts;
  std::vector<size_t> fins;
  size_t debut = 0;
  for (size_t i = 0; i + 1 < v.size(); i++)
  {
    if (estSynchro(v, i, t))
    {
      if (i > debut) { debuts.push_back(debut); fins.push_back(i); }
      debut = i + 2;
      i++;
    }
  }
  if (debut < v.size()) { debuts.push_back(debut); fins.push_back(v.size()); }

  // Decoder chaque segment et compter les messages identiques (repetitions)
  std::vector<std::string> valides;
  for (size_t s = 0; s < debuts.size(); s++)
  {
    std::string bits = decoderSegment(v, debuts[s], fins[s], t);
    bool ok = bits.size() == NB_BITS_ATTENDUS && bits.find('?') == std::string::npos;
    Serial.printf("DEC_RAW;%lu;%.1f;%u;%s;", (unsigned long)id, t, (unsigned)bits.size(), bits.c_str());
    if (ok) Serial.printf("%06lX", bitsVersValeur(bits));
    Serial.printf(";%s\n", ok ? "PROFIL_OK" : "INCOMPLET_OU_INVALIDE");
    if (ok) valides.push_back(bits);
  }

  // Resume : le message le plus repete de la capture
  if (valides.empty())
  {
    Serial.printf("DEC_RAW_RESUME;%lu;%.1f;%u;0;;0;AUCUN_MESSAGE\n", (unsigned long)id, t, (unsigned)debuts.size());
    return;
  }
  std::string meilleur;
  int maxRep = 0;
  for (const std::string& b : valides)
  {
    int rep = (int)std::count(valides.begin(), valides.end(), b);
    if (rep > maxRep) { maxRep = rep; meilleur = b; }
  }
  Serial.printf("DEC_RAW_RESUME;%lu;%.1f;%u;%u;%06lX;%d;%s\n", (unsigned long)id, t, (unsigned)debuts.size(),
                (unsigned)valides.size(), bitsVersValeur(meilleur), maxRep, maxRep >= 2 ? "FIABLE" : "NON_CONFIRME");
}

// ------------------------------------------------------------------
// Paquets FSK : le CC1101 a deja decode, on affiche l'etat et les bits
// ------------------------------------------------------------------
void analyserFSK(const uint8_t* d, size_t n, bool crcOK, bool tronque, uint32_t id)
{
  const char* etat = tronque ? "TRONQUE" : (!crcOK ? "CRC_KO" : (n ? "CRC_OK" : "VIDE"));
  Serial.printf("DEC_FSK;%lu;%u;%s;", (unsigned long)id, (unsigned)n, etat);
  for (size_t i = 0; i < n; i++) Serial.printf("%02X", d[i]);
  Serial.print(';');
  for (size_t i = 0; i < n; i++)
    for (int b = 7; b >= 0; b--) Serial.print((d[i] >> b) & 1);
  Serial.println();
}

// ------------------------------------------------------------------
// Tests sans radio : on fabrique des captures comme les produit A05
// ------------------------------------------------------------------
static std::vector<int32_t> fabriquerCapture(uint32_t mot, int repetitions)
{
  std::vector<int32_t> c;
  c.push_back(180);                        // 1re duree tronquee (debut de capture)
  for (int r = 0; r < repetitions; r++)
  {
    c.push_back(350);                      // synchro
    c.push_back(-10850);
    for (int i = 0; i < NB_BITS_ATTENDUS; i++)
    {
      bool b = (mot >> (NB_BITS_ATTENDUS - 1 - i)) & 1;
      c.push_back(b ? 1050 : 350);
      c.push_back(b ? -350 : -1050);
    }
  }
  c.pop_back();                            // le dernier bas n'est jamais mesure (pas de front apres)
  return c;
}

void testDecodeur()
{
  const uint32_t mot = 0xA5B32C;

  Serial.println("\n# TEST 1 : 3 repetitions -> attendu A5B32C, 3 rep, FIABLE");
  std::vector<int32_t> c = fabriquerCapture(mot, 3);
  analyserRAW(c.data(), c.size(), 0, 1);

  Serial.println("\n# TEST 2 : 1 seule repetition -> attendu A5B32C, NON_CONFIRME");
  c = fabriquerCapture(mot, 1);
  analyserRAW(c.data(), c.size(), 0, 2);

  Serial.println("\n# TEST 3 : capture coupee -> derniere repetition invalide, les autres OK");
  c = fabriquerCapture(mot, 3);
  c.resize(c.size() - 10);
  analyserRAW(c.data(), c.size(), 0, 3);

  Serial.println("\n# TEST 4 : impulsion incoherente dans la 2e repetition -> 1 '?', 2 rep FIABLE");
  c = fabriquerCapture(mot, 3);
  c[1 + 50 + 2 + 6] = 700;
  analyserRAW(c.data(), c.size(), 0, 4);

  Serial.println("\n# TEST 5 : debordement du buffer -> refuse");
  c = fabriquerCapture(mot, 3);
  analyserRAW(c.data(), c.size(), 1, 5);

  Serial.println("\n# TEST 6 : toutes les durees +10 % -> attendu A5B32C");
  c = fabriquerCapture(mot, 3);
  for (int32_t& x : c) x = x * 11 / 10;
  analyserRAW(c.data(), c.size(), 0, 6);

  Serial.println("\n# TEST 7 : parasites dans une impulsion haute -> attendu A5B32C");
  c = fabriquerCapture(mot, 3);
  c[3] = 600;                              // le 1er bit (1050 us) est coupe en 600 + parasites + 420
  c.insert(c.begin() + 4, 420);
  c.insert(c.begin() + 4, 30);
  c.insert(c.begin() + 4, -20);
  analyserRAW(c.data(), c.size(), 0, 7);

  Serial.println("\n# TEST 8 et 9 : paquets FSK CRC bon puis mauvais");
  uint8_t pkt[] = {0xA5, 0x32};
  analyserFSK(pkt, 2, true, false, 8);
  analyserFSK(pkt, 2, false, false, 9);
}