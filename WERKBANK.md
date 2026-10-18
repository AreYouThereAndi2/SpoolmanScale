# SpoolmanScale als Werkbank-Terminal: Anleitung (deutsch)

Dieser Fork erweitert die SpoolmanScale um:

- einen Barcode-Scanner,
- die Drucker-Steckdosen,
- eine Abfrage nach jedem Druck,
- das Anlegen neuer Spulen über [filament_to_bambuddy](https://github.com/AreYouThereAndi2/filament_to_bambuddy), auf Wunsch mit Preis.

Ausgelegt ist er auf **BamBuddy mit Spoolman dahinter**. Der Scanner funktioniert auch mit Spoolman direkt.

Inhalt:

1. [Was der Fork kann](#was-der-fork-kann)
2. [Teile](#teile)
3. [Verkabelung](#verkabelung)
4. [Firmware installieren](#firmware-installieren)
5. [Ersteinrichtung](#ersteinrichtung)
6. [Erst ohne Scanner testen, Scanner später nachrüsten](#erst-ohne-scanner-testen-scanner-später-nachrüsten)
7. [Updates](#updates)
8. [Einen Release machen](#einen-release-machen)
9. [Was im Gemini-Handoff nicht stimmte](#was-im-gemini-handoff-nicht-stimmte)
10. [Etikettendrucker TYPONOS PM240](#etikettendrucker-typonos-pm240)
11. [Noch offen](#noch-offen)

## Was der Fork kann

| Funktion | Wo | Backend |
|---|---|---|
| Barcode-Scanner (GM861, UART) | Weboberfläche → Einstellungen → Barcode-Scanner | alle; EAN-Suche braucht Spoolman |
| Spoolman-QR scannen → Spule laden, wiegen, danach AMS-Slot wählen | automatisch | alle; AMS-Slot nur BamBuddy |
| Unbekannten Tag per QR-Scan verknüpfen | Scan, während der Tag aufliegt | alle |
| EAN / Hersteller- / Amazon-Nummer → neue Spule anlegen, mit oder ohne Durchschnittspreis | Scan; Adresse von filament_to_bambuddy eintragen | BamBuddy |
| Power-Button pro Drucker | AMS-Ansicht, links neben dem Titel | BamBuddy |
| Abfrage nach dem Druck: erfolgreich? Spule leer? ausschalten? | automatisch; Schalter „Nach dem Druck fragen“ in den BamBuddy-Optionen | BamBuddy |
| Spule aus AMS-Slot austragen | AMS-Ansicht → Slot antippen → „Austragen“ (zweimal tippen) | BamBuddy |
| Etiketten auf dem TYPONOS PM240 (experimentell) | Einstellungen → Bluetooth → Drucker | alle |
| Zubehör an der Spule (Clip, RFID-Halter) zusätzlich abziehen | Weboberfläche → Einstellungen → Zubehör an der Spule; je Spule im Wiegen-Dialog | alle |

Alles bezieht sich auf den Drucker, der in der AMS-Ansicht gewählt ist (Chip „1/2 P1S“): der Power-Button, die Abfrage nach dem Druck und das Abschalten.

## Teile

Die vollständige Teileliste mit Links steht in [BUILDING.md](BUILDING.md#parts-list). Kurz:

| Teil | Modell |
|---|---|
| Display + Rechner | WT32-SC01 Plus (ESP32-S3, 3,5 Zoll) |
| NFC-Leser | PN532 (rote Platine) |
| Waagen-ADC | NAU7802 (Adafruit) |
| Wägezelle | YZC-133, 2 kg oder 5 kg |
| Barcode-Scanner (optional, kann später dazu) | GM861 mit UART-Ausgang |
| USB-C-Kabel | ein **Datenkabel**, kein reines Ladekabel |

Dazu das Gehäuse von MakerWorld (Link in BUILDING.md) und dünne Litze in fünf Farben.

## Verkabelung

Alles hängt am **I/O-Stecker** des WT32-SC01 Plus. Das mitgelieferte 7-polige Kabel wird **links bündig** in die 8-polige Buchse gesteckt.

### I/O-Stecker

| Pin | Farbe (Kabel) | Signal | geht an |
|---|---|---|---|
| 1 | rot | 5 V | PN532 VCC, NAU7802 VIN (Scanner VCC, siehe unten) |
| 2 | schwarz | GND | PN532 GND, NAU7802 GND, Scanner GND |
| 3 | gelb | GPIO 10 (SDA) | PN532 SDA, NAU7802 SDA |
| 4 | grün | GPIO 11 (SCL) | PN532 SCL, NAU7802 SCL |
| 5 | blau | – | frei |
| 6 | weiß | GPIO 13 | **TX des Scanners** |
| 7 | braun | GPIO 12 | PN532 RST |
| 8 | – (nicht am Kabel) | GPIO 21 | RX des Scanners, optional |

PN532 und NAU7802 hängen **parallel** am selben I2C-Bus. Den PN532 **nicht** über den STEMMA-QT-Durchgang des NAU7802 anschließen: Der liefert nur 3,3 V, der PN532 braucht 5 V.

### PN532

- **DIP-Schalter für I2C: 1 = ON, 2 = OFF.** (Im Gemini-Handoff stand es andersherum, das wäre SPI.)
- Von der **Rückseite** löten, vorne ist im Gehäuse kein Platz.
- Mit einem 5-poligen JST-SH-1,0-Stecker am PN532 kann man außerhalb des Gehäuses löten und später nur einstecken. Die Reihenfolge steht in [BUILDING.md](BUILDING.md#pn532--cable-pinout-jst-sh-10mm-5-pin-3rd-party-colors).

### NAU7802 und Wägezelle

Am sichersten direkt an die Pads VIN, GND, SDA, SCL löten. Wägezelle → NAU7802:

| Ader | Klemme |
|---|---|
| rot | E+ |
| schwarz | E− |
| weiß | A+ |
| grün | A− |

Zeigt die Waage negative Werte, A+ und A− tauschen.

### Barcode-Scanner (kann später dazu)

| Scanner | I/O-Pin |
|---|---|
| TX | 6 (weiß, GPIO 13) |
| GND | 2 (schwarz) |
| VCC | 1 (rot, 5 V), **nur wenn das Handbuch des Scanners 5 V erlaubt** |
| RX | 8 (GPIO 21), optional; nur nötig, um dem Scanner Befehle zu schicken |

> [!WARNING]
> Der ESP32-S3 verträgt an seinen Pins **keine 5 V**. Den Scanner erst an 5 V und GND anschließen und mit dem Multimeter seinen **TX gegen GND im Ruhezustand messen**. Liegt er bei etwa 3,3 V, darf er an Pin 6. Liegt er bei 5 V, braucht es einen Pegelwandler oder einen Spannungsteiler (z. B. 10 kΩ / 20 kΩ).

Der Scanner sendet jeden gelesenen Code als Textzeile mit 9600 Baud. Die meisten GM861 sind ab Werk so eingestellt.

## Firmware installieren

Die Firmware dieses Forks ist größer als 3 MB und braucht die 6-MB-Partitionstabelle. **Einmal per USB flashen**, danach kommen Updates über WLAN.

### Weg A: Web Flasher (empfohlen)

1. Am PC **Chrome oder Edge** öffnen (Firefox und Safari können kein WebUSB).
2. `https://areyouthereandi2.github.io/SpoolmanScale/` aufrufen. Voraussetzung: [GitHub Pages ist aktiviert](#einmalig-github-pages-einschalten) und der [erste Release](#einen-release-machen) ist gebaut.
3. Display per USB-C-**Datenkabel** direkt am PC anstecken, ohne Hub.
4. Auf den Knopf klicken, den Port wählen (meist „USB JTAG/serial debug unit“ oder „CH340“) und **Install** wählen.
   - Neues Board: Den Haken bei „Erase device“ setzen.
   - Bestehende Waage: Haken nicht setzen, dann bleiben WLAN, Kalibrierung und Backend erhalten.
5. Der Flasher kann danach gleich das WLAN übertragen. So muss man das Passwort nicht am kleinen Bildschirm tippen.

**„Failed to initialize“?** Andere Programme schließen, die den Port nutzen (Arduino IDE, Slicer, zweiter Tab). Dann auf der Rückseite **BOOT** halten, **RST** kurz drücken, **BOOT** loslassen und neu verbinden. Nach dem Flashen einmal **RST** drücken.

### Weg B: esptool (ohne Browser)

Die vier Dateien aus dem Ordner [`webflasher/`](webflasher) dieses Repos laden (nach dem ersten Release ist dort die Fork-Firmware) und flashen:

```bash
pip install esptool
esptool.py --chip esp32s3 --baud 921600 write_flash \
  0x0     bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 SpoolmanScale.bin
```

`SpoolmanScale_v….bin` aus einem Release ist **nur** die App. Sie reicht für ein Update, nicht für ein neues Board.

### Weg C: selbst bauen

```bash
git clone https://github.com/AreYouThereAndi2/SpoolmanScale.git
cd SpoolmanScale
pip install platformio
pio run -e wt32-sc01-plus -t upload
```

Details stehen in [BUILDING.md](BUILDING.md).

## Ersteinrichtung

1. **Sprache, Zeitzone, WLAN** am Bildschirm, falls der Web Flasher das WLAN nicht schon übertragen hat.
2. **Backend: BamBuddy.** URL eintragen, z. B. `http://zimaboard:8000`. Den API-Schlüssel in der **Weboberfläche** der Waage eintragen (die IP steht am Info-Bildschirm). Er braucht in BamBuddy → Settings → API Keys:
   - **Read Status** und **Manage Inventory** (Grundfunktionen),
   - **Control Printer** (Steckdosen),
   - **Manage Archives** (Bewertung nach dem Druck).
3. **Kalibrieren:** Einstellungen → Waage → Kalibrierung. Leer tarieren, ein bekanntes Gewicht von etwa 1000 g auflegen, Wert eingeben, speichern.
4. **Steckdose in BamBuddy** dem Drucker zuordnen (Settings → Smart Plugs).
   - Ist dort „Auto Power Off“ an, schaltet BamBuddy selbst ab, und die Waage sagt das nur.
   - Sonst wartet die Waage, bis Düse und Bett abgekühlt sind (Standard 50 °C).
5. **BamBuddy-Optionen** an der Waage: „Nach dem Wiegen ins AMS“ und „Nach dem Druck fragen“ einschalten.
6. **filament_to_bambuddy-Adresse** (Weboberfläche → Einstellungen → Barcode-Scanner), z. B. `http://zimaboard:8088`. Wird nur vom Scanner gebraucht und kann auch später eingetragen werden.

## Erst ohne Scanner testen, Scanner später nachrüsten

Der Scanner ist ab Werk **ausgeschaltet**. Ohne ihn läuft alles andere normal, Pin 6 kann frei bleiben.

**Testliste ohne Scanner:**

- [ ] Display startet, Touch reagiert, WLAN verbunden.
- [ ] Waage zeigt 0 g ohne Last, nach der Kalibrierung stimmt ein bekanntes Gewicht.
- [ ] Bambu-Spule auflegen: Tag wird gelesen, die Spule kommt aus BamBuddy/Spoolman, das Restgewicht wird geschrieben.
- [ ] Unbekannten Tag auflegen: Die Waage bietet Verknüpfen oder Anlegen an.
- [ ] AMS-Ansicht: Drucker wählen, Slots stimmen mit BamBuddy überein.
- [ ] Power-Button: Steckdose schaltet ein. Beim Ausschalten zeigt die Waage das Abkühlen bzw. den Hinweis auf Auto Power Off.
- [ ] Slot antippen → „Austragen“ → zweimal tippen: Die Zuordnung ist in BamBuddy weg.
- [ ] Nach einem Druck: Die Abfrage erscheint, die Bewertung steht danach im BamBuddy-Archiv.
- [ ] Info-Bildschirm zeigt `v0.9.0` (oder neuer).

Fehler bitte mit dem Log melden: Weboberfläche → Log, oder die SD-Karte (falls eingesetzt).

**Scanner nachrüsten:**

1. Waage vom USB trennen.
2. Scanner an 5 V und GND anschließen (nur wenn das Handbuch 5 V erlaubt), TX messen (siehe [Warnung oben](#barcode-scanner-kann-später-dazu)).
3. TX an Pin 6, optional RX an Pin 8.
4. Waage einschalten, Weboberfläche → Einstellungen → **Barcode-Scanner** → einschalten. Pins (RX 13, TX 21) und Baudrate (9600) passen im Normalfall.
5. Testen:
   - Spoolman-QR-Code einer Spule scannen: Die Spule wird geladen.
   - Box-EAN scannen: Die Waage findet die Spule oder bietet an, sie anzulegen.
   - Erscheint nichts, im Log nach `Scan:` suchen. Kommen dort unlesbare Zeichen an, stimmt die Baudrate nicht.

## BamBuddy mit Spoolman: welches Inventar?

BamBuddy antwortet unter zwei Adressen: mit seinem eigenen Inventar (`/api/v1/inventory`) oder, wenn in BamBuddy Spoolman eingeschaltet ist, mit Spoolman dahinter (`/api/v1/spoolman/inventory`). Die Waage fragt BamBuddy, welches gilt (`/api/v1/settings/spoolman`). Das beantwortet BamBuddy aber nur einem API-Schlüssel mit **„Read Status“**. Ohne diese Berechtigung nahm die Waage stillschweigend das eigene Inventar, was hinter Spoolman falsch ist.

**Weboberfläche → Backend → BamBuddy-Inventar:**

- **Automatisch (BamBuddy fragen):** wie bisher. Darunter steht, was gerade gilt, oder in Rot „BamBuddy verweigert die Abfrage (HTTP 403)“.
- **Spoolman (über BamBuddy):** fest, ohne zu fragen.
- **BamBuddys eigenes Inventar:** fest.

filament_to_bambuddy hat denselben Schalter, unten auf seiner Seite. Mit BamBuddy + Spoolman bei beiden **Spoolman** wählen oder dem Schlüssel „Read Status“ geben.

## Zubehör an der Spule abziehen (Clip, RFID-Halter)

Die Waage kennt das Leergewicht der Spule aus FilaMan, Spoolman oder BamBuddy. Ein Filament-Clip oder ein RFID-Halter an der Spule wiegt zusätzlich ein paar Gramm, und die zieht die Waage auf Wunsch ebenfalls ab.

1. **Weboberfläche → Einstellungen → Zubehör an der Spule:** Bis zu 8 Teile mit Namen und Gewicht eintragen, z. B. „Clip 1“ = 2 g, „Clip 2“ = 5 g, „RFID-Halter 1“ = 3 g. Bis zu **3** davon als **Standard** ankreuzen, dann **Speichern**. Ein leerer Name gibt den Platz frei.
2. **Der Standard** gilt für jede Spule, für die nichts anderes gewählt ist.
3. **Je Spule:** Im Wiegen-Dialog steht oben rechts z. B. `✎ -5 g`. Antippen öffnet die Auswahl: bis zu 3 Teile antippen, dann **OK**. Die Waage merkt sich die Auswahl für diese Spule und zeigt den Dialog mit den neuen Zahlen wieder an.

Abgezogen wird überall, wo aus der Anzeige ein Filamentgewicht wird: Netto auf dem Hauptbildschirm, die Knöpfe im Wiegen-Dialog, automatisches Wiegen, die AMS-Abfrage, die Spulenwahl nach Gewicht und **Leerspule wiegen**. Wird also eine leere Spule mit Clip gewogen, landet nur die Spule als Leergewicht im Backend. Die Brutto-Angabe an FilaMan ist das Gewicht ohne Zubehör, so wie beim Beutel.

Der Abzug ist eine Einstellung der Waage. Er liegt nicht im Backend: Eine zweite Waage oder BamBuddy selbst wissen nichts davon.

## Updates

- **An der Waage:** Einstellungen → System → Firmware Update → **Update über GitHub**. Die Waage holt den neuesten Release aus diesem Fork und prüft die Prüfsumme vor dem Neustart.
- **Im Browser:** Weboberfläche → Firmware. Dort stehen auch die Release-Notes, und man kann eine `.bin` selbst hochladen.
- Die Waage zeigt einen Punkt, wenn es eine neue Version gibt. Sie liest dafür `https://areyouthereandi2.github.io/SpoolmanScale/version.json`.

> Kein Update aus dem Original-Repo (Niko11111) einspielen: Es würde die Funktionen dieses Forks entfernen.

### Einmalig: GitHub Pages einschalten

Web Flasher und Update-Prüfung laufen über GitHub Pages:

1. Repo auf GitHub → **Settings → Pages**.
2. „Build and deployment“: Quelle **Deploy from a branch**, Branch **main**, Ordner **/ (root)**, speichern.
3. Nach ein bis zwei Minuten ist `https://areyouthereandi2.github.io/SpoolmanScale/` erreichbar.

## Einen Release machen

1. `FW_VERSION` in `src/app_config.h` und die Versionszeile oben in `src/main.cpp` erhöhen. Schema:
   - `v0.9.1`, `v0.10.0`, …: öffentliche Version. Sie aktualisiert den Web Flasher und wird allen Waagen angeboten.
   - `v0.9.1-beta.1`, `.2`, …: Zwischenstand vor `v0.9.1`. Nur Vorabversion: Angeboten wird er nur Waagen mit eingeschalteten Vorabversionen, und der Web Flasher bleibt beim letzten öffentlichen Stand.
   - Ein reines `-beta` ohne Zähler lehnt `scripts/check.sh` ab.
2. Release-Notes als `.github/release-notes/<version>.md` anlegen.
3. Per Pull Request nach `main` bringen.
4. Auf `main` einen Tag mit genau der Version pushen, z. B. `git tag v0.9.1 && git push origin v0.9.1`.
5. Der Workflow „Build and Release“ baut die Firmware, prüft Größe und Version und legt den Release mit den Notes an. Bei öffentlichen Versionen aktualisiert er außerdem Web Flasher und `version.json` auf `main`.

## Was im Gemini-Handoff nicht stimmte

- **Pins:** GPIO 5/6 ist der interne Touch-Bus. Waage und NFC hängen an **GPIO 10/11**, der Scanner an **GPIO 13 (Pin 6, weiß)**.
- **DIP-Schalter am roten PN532:** Für I2C ist **1 = ON, 2 = OFF** richtig. „1 = 0, 2 = 1“ stellt SPI ein.
- **Versorgung:** Der Anschluss liefert nur 5 V und GND. Den TX-Pegel des Scanners vorher messen, er muss bei 3,3 V liegen.
- **Spoolman `PATCH gross_weight`** gibt es nicht. Gewogen wird über `PUT /spool/{id}/measure`, so wie es die Firmware immer schon macht.
- **BamBuddy-Endpunkte:** `/printers/{id}/power`, `safe_cooldown`, `/prints/{id}/finish` und `/printers/{id}/clear_print` gibt es nicht. Verwendet werden, geprüft am BamBuddy-Quellcode 1.2.5.7:
  - `GET /smart-plugs/by-printer/{id}`, `GET /smart-plugs/{id}/status`, `POST /smart-plugs/{id}/control` mit `{"action":"on"|"off"}`
  - `GET /printers/{id}/status` (Temperaturen, Zustand)
  - `GET /archives/?printer_id=X&limit=1` und `PATCH /archives/{id}` mit `user_verdict` `good`/`reject`
  - `POST /spoolman/inventory/spools` mit `cost_per_kg` (neue Spule mit Preis)
  - AMS: `/spoolman/inventory/slot-assignments` (war schon in der Firmware)

## Etikettendrucker TYPONOS PM240

Der PM240 spricht **TSPL**, die Befehlssprache vieler Etikettendrucker. Die Hersteller-App (Labelnize) schickt sie über klassisches Bluetooth. Der ESP32-S3 kann nur Bluetooth Low Energy, aber der PM240 bietet denselben Dienst auch per BLE an (`0xFF00`, geschrieben wird `0xFF02`, wie bei den Phomemo-Druckern). Das Protokoll stammt aus einem Mitschnitt der App. Ein Druckauftrag der Waage ist Byte für Byte derselbe wie der der App für das gleiche Bild:

```
SIZE 50.0 mm,30.0 mm
GAP 6.0 mm,0 mm
DIRECTION 0,0
DENSITY 10
CLS
BITMAP 0,0,48,240,1,<48 Bytes x 240 Zeilen, Bit 0 = schwarz>
PRINT 1
```

Der Kopf ist 48 mm breit (384 Punkte bei 203 dpi). Bei 50-mm-Etiketten druckt er die mittleren 48 mm, die Waage schneidet je 1 mm links und rechts ab. Dort liegt ohnehin ihr Rand.

**Einrichten:**

1. Die Drucker-App am Handy schließen bzw. die Verbindung trennen. Der PM240 nimmt nur eine BLE-Verbindung zur Zeit an.
2. An der Waage: Einstellungen → **Bluetooth** einschalten. Die Waage startet dafür einmal neu.
3. Bluetooth → Geräte suchen → **PM240** antippen → als Drucker verwenden.
4. Bluetooth → **Drucker** → Modell antippen, bis **PM240** erscheint. Etikett **50 x 30 mm**. In der Weboberfläche heißt das Modell „TYPONOS PM240“.
5. Testdruck.

**Noch nicht am Gerät getestet.** Wenn etwas schiefgeht, bitte das Log schicken, die Zeilen beginnen mit `TSPL:` und `BLE:`:

- „kein unterstützter Drucker“: Die Waage hat unter `0xFF00` kein beschreibbares `0xFF02` gefunden. Laut nRF Connect hat der PM240 es (Write No Response). Die Meldung heißt dann eher, dass eine falsche Adresse gewählt ist.
- Druck verschoben oder leer: Lücke (6 mm) und Dichte (10) sind die Werte der App für die 50 x 30-mm-Rolle. Andere Rollen brauchen eventuell andere Werte.

## Noch offen

- **Hardware-Test:** Bisher ist alles nur kompiliert und mit den Prüfskripten getestet, noch nicht auf dem Gerät.
