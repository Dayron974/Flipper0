# Analyseur radio 433 MHz — Tab5

Projet de conception d'un analyseur portable de communications radio autour de **433 MHz**, basé sur un **M5Stack Tab5** et un transceiver **CC1101**.

L'objectif est de réaliser un outil capable de détecter et d'analyser des transmissions radio Sub-GHz à l'aide de l'écran tactile du Tab5.

## Fonctionnalités visées

- Configuration du CC1101 depuis le Tab5
- Sélection et balayage de fréquences autour de 433 MHz
- Mesure du RSSI
- Détection d'une transmission radio
- Réception de signaux OOK/ASK et FSK
- Capture de données brutes
- Visualisation temporelle du signal reçu
- Analyse des durées d'impulsions et des répétitions de trames
- Sauvegarde des captures sur carte microSD
- Interface graphique tactile sur le Tab5
- Émission dans le cadre de tests réalisés sur nos propres équipements

À terme, le CC1101 doit pouvoir être remplacé par un **transceiver 433 MHz développé dans le cadre du projet**.

---

## Matériel

- M5Stack Tab5
- Module CC1101 adapté à la bande 433 MHz
- Antenne 433 MHz
- Carte microSD
- Connexion SPI entre le Tab5 et le CC1101

Architecture générale :

```text
Antenne
   │
   ▼
CC1101
   │
   │ SPI + GPIO
   ▼
M5Stack Tab5
   │
   ├── Interface graphique
   ├── Scanner RF
   ├── Acquisition
   ├── Analyse des signaux
   └── Stockage microSD
```

---

## Environnement de développement

Le projet utilise :

- **Visual Studio Code**
- **PlatformIO**
- **Arduino Framework**
- **C/C++**

### Installation de PlatformIO

1. Installer [Visual Studio Code](https://code.visualstudio.com/).
2. Ouvrir l'onglet **Extensions**.
3. Rechercher `PlatformIO IDE`.
4. Installer l'extension **PlatformIO IDE**.
5. Redémarrer Visual Studio Code si nécessaire.
6. Ouvrir directement le dossier du projet dans VS Code.

PlatformIO utilise automatiquement le fichier `platformio.ini` situé à la racine du projet.

## Bibliothèques

Les bibliothèques sont déclarées directement dans `platformio.ini` :

### M5Unified

```ini
https://github.com/M5Stack/M5Unified.git
```

Bibliothèque principale de M5Stack permettant notamment l'accès aux périphériques du Tab5.

### M5GFX

```ini
https://github.com/M5Stack/M5GFX.git
```

Utilisée pour la gestion de l'affichage et de l'interface graphique.

### RadioLib

```ini
jgromes/RadioLib
```

Bibliothèque utilisée pour piloter les transceivers radio, notamment le **CC1101**.

### Installation

Il n'est normalement pas nécessaire d'installer ces bibliothèques manuellement.

Après ouverture du projet, PlatformIO télécharge les dépendances déclarées dans :

```ini
lib_deps =
```

Lors de la première compilation.

---

## Compiler le projet

Depuis VS Code :

1. Ouvrir le projet.
2. Attendre la fin de l'installation des dépendances PlatformIO.
3. Cliquer sur **Build** dans la barre PlatformIO.

Ou depuis un terminal PlatformIO :

```bash
pio run
```

---

## Téléverser sur le Tab5

Connecter le Tab5 en USB puis utiliser :

```bash
pio run -t upload
```

Ou le bouton **Upload** de PlatformIO.

## Moniteur série

Le projet utilise une liaison série à :

```text
115200 bauds
```

Pour ouvrir le moniteur :

```bash
pio device monitor
```

---

## Développement

Le développement est organisé autour de quatre axes principaux :

- communication entre le Tab5 et le CC1101 ;
- acquisition et caractérisation des signaux 433 MHz ;
- interface graphique et stockage des captures ;
- conception d'un transceiver 433 MHz destiné à remplacer à terme le CC1101.

---

## Remarque

Les fonctions d'émission doivent être utilisées uniquement sur des systèmes et équipements de test autorisés, en respectant les fréquences, puissances et conditions d'utilisation applicables à la bande utilisée.
