# SpoolmanScale als Werkbank-Terminal: Anleitung (deutsch)

Dieser Fork erweitert die SpoolmanScale um:

- einen Barcode-Scanner,
- die Drucker-Steckdosen,
- eine Abfrage nach jedem Druck,
- das Anlegen neuer Spulen über [filament_to_bambuddy](https://github.com/AreYouThereAndi2/filament_to_bambuddy), auf Wunsch mit Preis.

Ausgelegt ist er auf **BamBuddy mit Spoolman dahinter**. Der Scanner funktioniert auch mit Spoolman direkt.

Inhalt:

1. [Was der Fork kann](#was-der-fork-kann)
2. [Teile und Werkzeug](#teile-und-werkzeug)
3. [So hängt alles zusammen](#so-hängt-alles-zusammen)
4. [Aufbau in Stufen](#aufbau-in-stufen): 0 Display, 1 Waage, 2 NFC, 3 Gehäuse, 4 Scanner, 5 Etikettendrucker
5. [Firmware installieren](#firmware-installieren)
6. [Ersteinrichtung](#ersteinrichtung)
7. [Bedienen wie am Handy](#bedienen-wie-am-handy)
8. [Funktionstest nach dem Aufbau](#funktionstest-nach-dem-aufbau)
9. [BamBuddy mit Spoolman: welches Inventar?](#bambuddy-mit-spoolman-welches-inventar)
10. [Updates](#updates)
11. [Einen Release machen](#einen-release-machen)
12. [Was im Gemini-Handoff nicht stimmte](#was-im-gemini-handoff-nicht-stimmte)
13. [Etikettendrucker TYPONOS PM240](#etikettendrucker-typonos-pm240)
14. [Noch offen](#noch-offen)

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

## Teile und Werkzeug

Die vollständige Teileliste mit Links steht in [BUILDING.md](BUILDING.md#parts-list).

| Teil | Modell | Für Stufe |
|---|---|---|
| Display + Rechner | WT32-SC01 Plus (ESP32-S3, 3,5 Zoll, mit 7-poligem I/O-Kabel) | 0 |
| USB-C-Kabel | ein **Datenkabel**, kein reines Ladekabel | 0 |
| Waagen-ADC | NAU7802 (Adafruit oder Nachbau) | 1 |
| Wägezelle | YZC-133, 2 kg oder 5 kg | 1 |
| NFC-Leser | PN532 (rote Platine, mit DIP-Schaltern) | 2 |
| Gehäuse, Schrauben, USB-C-Einbaubuchse | siehe BUILDING.md | 3 |
| Barcode-Scanner (optional) | GM861 mit 6-poligem SH-1,0-Kabel | 4 |
| 3,3-V-Spannungsregler (nur für den Scanner) | kleines AMS1117-3.3- oder AP2112-3.3-Modul | 4 |
| Etikettendrucker (optional) | TYPONOS PM240, Bluetooth, **ohne Kabel** | 5 |

**Werkzeug:** Lötkolben mit feiner Spitze, Lötzinn, Abisolierzange, Seitenschneider, **Multimeter**, Schrumpfschlauch oder Kapton-Band. Dazu dünne Litze in fünf Farben (rot, schwarz, gelb, grün, weiß, je 30 bis 40 cm).

## So hängt alles zusammen

Alles hängt an **einer Buchse**: dem **I/O-Stecker** auf der Rückseite des WT32-SC01 Plus. Dort steckt das mitgelieferte **7-polige Kabel**, **links bündig** in der 8-poligen Buchse. Der achte Kontakt (ganz rechts) bleibt frei, er wird nur für eine Option beim Scanner gebraucht.

| Pin | Kabelfarbe | Signal | Stufe 1: Waage | Stufe 2: NFC | Stufe 4: Scanner |
|---|---|---|---|---|---|
| 1 | rot | **5 V** | NAU7802 VIN | PN532 VCC | Eingang des 3,3-V-Reglers |
| 2 | schwarz | **GND** | NAU7802 GND | PN532 GND | Regler GND + Scanner GND |
| 3 | gelb | SDA (GPIO 10) | NAU7802 SDA | PN532 SDA | – |
| 4 | grün | SCL (GPIO 11) | NAU7802 SCL | PN532 SCL | – |
| 5 | blau | – | – | – | – |
| 6 | weiß | GPIO 13 | – | – | **Scanner TXD** |
| 7 | braun | GPIO 12 | – | PN532 RSTO/RST | – |
| 8 | (nicht am Kabel) | GPIO 21 | – | – | Scanner RXD (optional) |

```
WT32-SC01 Plus, I/O-Kabel
 rot    5V ──┬──────────── NAU7802 VIN
             ├──────────── PN532 VCC
             └── [3,3-V-Regler] ── GM861 VCC (Pin 1)
 schwarz GND ─┬─────────── NAU7802 GND
              ├─────────── PN532 GND
              └─────────── Regler GND, GM861 GND (Pin 4)
 gelb   SDA ──┬─────────── NAU7802 SDA
              └─────────── PN532 SDA
 grün   SCL ──┬─────────── NAU7802 SCL
              └─────────── PN532 SCL
 weiß   IO13 ───────────── GM861 TXD (Pin 2)
 braun  IO12 ───────────── PN532 RST
```

Drei Regeln, die Schäden vermeiden:

1. **Immer stromlos arbeiten.** USB abziehen, bevor du etwas an- oder absteckst.
2. **Kein 5-V-Signal an einen Datenpin.** Der ESP32-S3 verträgt an GPIO 10 bis 13 und 21 nur 3,3 V. 5 V gehören nur an Pin 1.
3. **NAU7802 und PN532 parallel anschließen**, beide direkt an Pin 1 bis 4. Den PN532 **nicht** über die STEMMA-QT-Buchse des NAU7802 anschließen: Die liefert nur 3,3 V, der PN532 braucht 5 V.

## Aufbau in Stufen

Baue und teste eine Stufe nach der anderen. Funktioniert eine Stufe, kommt die nächste dazu. Dann weißt du bei einem Fehler immer, dass er in der letzten Stufe steckt.

### Stufe 0: Nur das Display (ohne Kabel)

1. Firmware flashen, siehe [Firmware installieren](#firmware-installieren).
2. Am Bildschirm Sprache, Zeitzone und WLAN einstellen (oder das WLAN vom Web Flasher übertragen lassen).
3. Backend einrichten, siehe [Ersteinrichtung](#ersteinrichtung).

**Prüfen:**

- [ ] Das Display startet, Touch reagiert.
- [ ] WLAN verbunden, der Info-Bildschirm zeigt die IP-Adresse.
- [ ] Die Weboberfläche öffnet sich im Browser unter dieser IP.

Oben erscheint jetzt **„Kein Modul am I2C-Bus“**. Das ist richtig, es ist ja noch nichts angeschlossen.

### Stufe 1: Waage (NAU7802 + Wägezelle)

1. **Wägezelle an den NAU7802** (an die Schraubklemmen oder Lötpads):

   | Ader der Wägezelle | NAU7802 |
   |---|---|
   | rot | E+ |
   | schwarz | E− |
   | weiß | A+ |
   | grün | A− |

2. **NAU7802 an das I/O-Kabel**, am besten direkt an die beschrifteten Pads löten:

   | NAU7802 | I/O-Kabel |
   |---|---|
   | VIN | Pin 1, rot (5 V) |
   | GND | Pin 2, schwarz |
   | SDA | Pin 3, gelb |
   | SCL | Pin 4, grün |

   Fertige STEMMA-QT-Kabel anderer Hersteller haben oft eine **andere Farbfolge** als das WT32-Kabel. Geh nach der Beschriftung (VIN, GND, SDA, SCL), nicht nach der Farbe.
3. Alle Lötstellen einzeln mit Schrumpfschlauch isolieren.
4. USB anstecken.

**Prüfen:**

- [ ] Die Meldung „Kein Modul am I2C-Bus“ ist weg. Jetzt darf **„NFC-Reader fehlt (PN532)“** dastehen, der kommt erst in Stufe 2.
- [ ] Die Waage zeigt ungefähr 0 g. Mit der Hand leicht drücken lässt die Zahl **steigen**.
- [ ] Kalibrieren: Einstellungen → Waage → Kalibrierung. Leer tarieren, ein bekanntes Gewicht (z. B. 1000 g, eine volle Wasserflasche auf der Küchenwaage abgewogen) auflegen, Wert eingeben, speichern.
- [ ] Danach stimmt das bekannte Gewicht auf ±2 g.

**Wenn es nicht geht:**

| Anzeige | Ursache und Abhilfe |
|---|---|
| „Kein Modul am I2C-Bus“ bleibt | 5 V oder GND fehlen, oder SDA und SCL sind vertauscht. Mit dem Multimeter zwischen VIN und GND am NAU7802 messen: etwa 5 V? |
| „Wägezelle verpolt (A+/A−)“ oder negative Werte beim Drücken | A+ und A− tauschen (weiß und grün). |
| „Wägezelle unruhig“ | Eine Ader hat Wackelkontakt, oder die Zelle berührt das Gehäuse. Lötstellen prüfen, die Zelle frei schweben lassen. |
| „Waage nicht kalibriert“ | Kalibrierung wie oben machen. |

Ein Gerät ganz ohne Waage (nur NFC)? Dann in Einstellungen → Waage **„Waage vorhanden“** ausschalten, die Meldung verschwindet.

### Stufe 2: NFC-Leser (PN532)

1. USB abziehen.
2. **DIP-Schalter am PN532 auf I2C stellen: Schalter 1 = ON, Schalter 2 = OFF.** Steht er anders (HSU oder SPI), antwortet der Leser gar nicht.
3. **Von der Rückseite löten**, vorne ist im Gehäuse kein Platz. Die fünf Drähte:

   | PN532 | I/O-Kabel |
   |---|---|
   | VCC | Pin 1, rot (5 V) |
   | GND | Pin 2, schwarz |
   | SDA | Pin 3, gelb |
   | SCL | Pin 4, grün |
   | RSTO bzw. RST | Pin 7, braun |

   Am bequemsten: an den PN532 ein kurzes Kabel mit **5-poligem JST-SH-1,0-Stecker** löten. Dann geht das Löten außerhalb des Gehäuses, und beim Zusammenbau wird nur eingesteckt. Die Belegung solcher Fremdkabel steht in [BUILDING.md](BUILDING.md#pn532--cable-pinout-jst-sh-10mm-5-pin-3rd-party-colors).
4. Die Drähte von NAU7802 und PN532 treffen sich an Pin 1 bis 4: beide zusammen an den jeweiligen Draht des I/O-Kabels löten (parallel), nicht hintereinander.
5. USB anstecken.

**Prüfen:**

- [ ] Keine Meldung mehr oben.
- [ ] Eine Bambu-Spule (oder ein NFC-Tag) auf die Waage legen: Nach 1 bis 2 Sekunden wird der Tag gelesen. Eine bekannte Spule erscheint mit Name und Restgewicht.
- [ ] Ein unbekannter Tag: Die Waage bietet **Verknüpfen** oder **Anlegen** an.

**Wenn es nicht geht:**

| Anzeige | Ursache und Abhilfe |
|---|---|
| „NFC-Reader fehlt (PN532)“ | DIP-Schalter prüfen (1 = ON, 2 = OFF). VCC muss an **5 V** hängen, nicht am STEMMA-QT-Ausgang des NAU7802. |
| „Waagen-ADC fehlt (NAU7802)“, seit der PN532 dran ist | Beim Löten ist am NAU7802 etwas abgegangen, oder SDA und SCL sind am PN532 vertauscht und stören den Bus. |
| Tag wird nicht gelesen | Den Tag mittig auflegen. Der Leser sitzt unter der Mitte der Waagschale, Metall dazwischen schirmt ab. |

### Stufe 3: Ins Gehäuse

Erst wenn Stufe 1 und 2 funktionieren. Die Reihenfolge und Fotos stehen in [BUILDING.md](BUILDING.md#assembly-steps). Worauf es ankommt:

1. Die USB-C-Einbaubuchse kürzen, bis sie nicht über den Displayrand ragt (mit dem Cuttermesser in kleinen Schritten).
2. Die Kabel so verlegen, dass sie **nicht an die Waagschale drücken**. Sonst wiegt die Waage das Kabel mit, und das Gewicht schwankt.
3. Nach dem Zuschrauben die Kalibrierung aus Stufe 1 **noch einmal** machen: Das Gehäuse verändert den Nullpunkt.

### Stufe 4: Barcode-Scanner (optional)

Der Scanner liest Barcodes auf Filament-Kartons und QR-Codes auf Etiketten. Er ist in der Firmware **ab Werk ausgeschaltet**. Ohne ihn läuft alles andere normal.

> [!WARNING]
> Der GM861 läuft laut Handbuch mit **3,3 V** (höchstens 70 mA). Der I/O-Stecker hat aber nur **5 V**. Den Scanner **nicht direkt an Pin 1** hängen. Dazwischen gehört ein kleiner 3,3-V-Regler. Läuft der Scanner mit 3,3 V, sendet er auch mit 3,3 V und darf direkt an GPIO 13.

Der GM861 hat ein 6-poliges SH-1,0-Kabel. **Die Farben der Adern sagen nichts**, es zählt die Reihenfolge am Stecker (Pin 1 ist auf der Platine markiert):

| GM861 Pin | Signal | geht an |
|---|---|---|
| 1 | VCC (3,3 V) | Ausgang (OUT/3V3) des 3,3-V-Reglers |
| 2 | TXD | **Pin 6 (weiß, GPIO 13)** |
| 3 | RXD | Pin 8 (GPIO 21), **optional** |
| 4 | GND | Pin 2 (schwarz) |
| 5 | D− (USB) | frei lassen |
| 6 | D+ (USB) | frei lassen |

Der 3,3-V-Regler (z. B. AMS1117-3.3-Modul): **IN** an Pin 1 (rot, 5 V), **GND** an Pin 2 (schwarz), **OUT** an GM861 Pin 1.

RXD ist nur nötig, wenn die Waage dem Scanner etwas sagen soll: Licht und grüne LED aus der Weboberfläche einstellen. Pin 8 ist nicht am 7-poligen Kabel, dafür einen einzelnen Draht an den achten Kontakt der Buchse (z. B. mit einer einzelnen JST-Crimp-Hülse).

**Schritt für Schritt:**

1. USB abziehen.
2. Den 3,3-V-Regler an Pin 1 und 2 anschließen, **noch ohne Scanner**. USB anstecken und am Ausgang (OUT gegen GND) messen: **3,2 bis 3,4 V**? Wieder abziehen.
3. Den Scanner anschließen: VCC an OUT des Reglers, GND an Pin 2, TXD an Pin 6. Optional RXD an Pin 8.
4. USB anstecken. Weboberfläche → Einstellungen → **Barcode-Scanner** → einschalten. Pins (RX 13, TX 21) und 9600 Baud passen im Normalfall, also speichern.
5. Optional: die **filament_to_bambuddy-Adresse** eintragen (gleiche Karte), z. B. `http://zimaboard:8088`. Ohne sie kennt die Waage nur Codes, die schon an einer Spule hängen.

**Prüfen:**

- [ ] Einen beliebigen Barcode vor den Scanner halten: In der Weboberfläche steht er unter **Letzter Code**.
- [ ] Den QR-Code eines Spulen-Etiketts scannen: Die Spule wird geladen.
- [ ] Den Barcode eines Filament-Kartons scannen: Die Waage findet die Spule oder bietet an, sie anzulegen.

**Wenn es nicht geht:**

| Anzeige | Ursache und Abhilfe |
|---|---|
| Scanner leuchtet nicht | Keine 3,3 V am Scanner: Regler und Pin 1/4 am Scanner-Stecker prüfen. |
| Scanner leuchtet und piept, aber „Letzter Code“ bleibt leer | TXD steckt nicht an Pin 6, oder TXD und RXD sind vertauscht. Scanner in der Weboberfläche eingeschaltet? |
| „Letzter Code“ zeigt Zeichensalat | Die Baudrate stimmt nicht. Der GM861 hat ab Werk 9600. |
| Licht/LED-Einstellung: „keine Antwort“ | RXD fehlt an Pin 8, oder in der Weboberfläche ist kein TX-Pin (21) eingetragen. |

Hat dein Scanner eine eigene Platine, auf der **5 V** steht, gilt das Handbuch dieser Platine. Dann an 5 V betreiben und vorher mit dem Multimeter **TXD gegen GND im Ruhezustand** messen: Nur bei etwa 3,3 V darf er an Pin 6, sonst braucht es einen Pegelwandler oder Spannungsteiler (10 kΩ / 20 kΩ).

### Stufe 5: Etikettendrucker (optional, ohne Kabel)

Der TYPONOS PM240 verbindet sich per **Bluetooth**, es gibt nichts zu verdrahten.

1. Drucker einschalten und eine Etikettenrolle einlegen (Standard 50 × 30 mm).
2. An der Waage: Einstellungen → Bluetooth → **Drucker** → suchen und den PM240 wählen. Die Rollengröße einstellen.
3. Testetikett drucken. Sitzt es schief, mit dem Kalibrierausdruck die Verschiebung einstellen.

Steht filament_to_bambuddy in den Einstellungen, druckt die Waage das Etikett im Layout, das du am Handy angeordnet hast. Mehr dazu unter [Etikettendrucker TYPONOS PM240](#etikettendrucker-typonos-pm240).

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

   Läuft BamBuddy mit Spoolman in Docker, unter **Backend → Spoolman-Adresse für die Waage** die LAN-Adresse von Spoolman eintragen (z. B. `http://192.168.1.5:7912`), siehe [unten](#bambuddy-mit-spoolman-welches-inventar).
3. **Kalibrieren:** Einstellungen → Waage → Kalibrierung. Leer tarieren, ein bekanntes Gewicht von etwa 1000 g auflegen, Wert eingeben, speichern.
4. **Steckdose in BamBuddy** dem Drucker zuordnen (Settings → Smart Plugs).
   - Ist dort „Auto Power Off“ an, schaltet BamBuddy selbst ab, und die Waage sagt das nur.
   - Sonst wartet die Waage, bis Düse und Bett abgekühlt sind (Standard 50 °C).
5. **BamBuddy-Optionen** an der Waage: „Nach dem Wiegen ins AMS“ und „Nach dem Druck fragen“ einschalten.
6. **filament_to_bambuddy-Adresse** (Weboberfläche → Einstellungen → Barcode-Scanner), z. B. `http://zimaboard:8088`. Wird nur vom Scanner gebraucht und kann auch später eingetragen werden.

## Bedienen wie am Handy

Die Waage kennt zwei Gesten. Die Knöpfe bleiben trotzdem da.

| Geste | Was passiert |
|---|---|
| Vom **linken Rand** nach rechts wischen | Zurück, wie der Zurück- oder Schließen-Knopf oben links. Ist das Schnellmenü offen, schließt es zuerst. |
| Vom **oberen Rand** nach unten wischen, oder den kleinen Strich oben in der Mitte antippen | Das **Schnellmenü** fährt von oben herein. |
| Im Schnellmenü nach **oben** wischen, daneben tippen oder zurückwischen | Das Schnellmenü fährt wieder hoch. |

Im Schnellmenü:

- WLAN und IP-Adresse, dazu das verbundene Backend.
- Ein Regler für die Helligkeit. Er wird sofort gespeichert.
- Vier Kacheln: **Tara**, **AMS**, **Info** und **Einstellungen**.

Kurze Hinweise:

- Die Wisch-Gesten müssen ganz am Rand beginnen, in den äußersten 2 bis 3 mm des Glases. So wird ein Wischen in einer Liste nicht mit Zurück verwechselt.
- Bei einer Frage, die auf eine Antwort wartet (zum Beispiel nach dem Druck), öffnet sich das Schnellmenü nicht.
- Fenster und Fragen gleiten jetzt kurz von unten herein, wie eine Karte am Handy.

## Funktionstest nach dem Aufbau

Die Hardware-Tests stehen bei den [Stufen](#aufbau-in-stufen). Danach die Funktionen mit BamBuddy:

- [ ] Bambu-Spule auflegen: Tag wird gelesen, die Spule kommt aus BamBuddy/Spoolman, das Restgewicht wird geschrieben.
- [ ] Unbekannten Tag auflegen: Die Waage bietet Verknüpfen oder Anlegen an.
- [ ] AMS-Ansicht: Drucker wählen, die Slots stimmen mit BamBuddy überein.
- [ ] Power-Button: Die Steckdose schaltet ein. Beim Ausschalten zeigt die Waage das Abkühlen bzw. den Hinweis auf Auto Power Off.
- [ ] Slot antippen → „Austragen“ → zweimal tippen: Die Zuordnung ist in BamBuddy weg.
- [ ] Nach einem Druck: Die Abfrage erscheint, die Bewertung steht danach im BamBuddy-Archiv.
- [ ] Der Info-Bildschirm zeigt die aktuelle Version.
- [ ] Vom oberen Rand nach unten wischen: Das Schnellmenü erscheint. Nach oben wischen: Es verschwindet.
- [ ] In den Einstellungen vom linken Rand nach rechts wischen: Die Waage geht eine Seite zurück.

Fehler bitte mit dem Log melden: Weboberfläche → Log, oder die SD-Karte (falls eingesetzt).

## BamBuddy mit Spoolman: welches Inventar?

BamBuddy antwortet unter zwei Adressen: mit seinem eigenen Inventar (`/api/v1/inventory`) oder, wenn in BamBuddy Spoolman eingeschaltet ist, mit Spoolman dahinter (`/api/v1/spoolman/inventory`). Die Waage fragt BamBuddy, welches gilt (`/api/v1/settings/spoolman`). Das beantwortet BamBuddy aber nur einem API-Schlüssel mit **„Read Status“**. Ohne diese Berechtigung nahm die Waage stillschweigend das eigene Inventar, was hinter Spoolman falsch ist.

**Weboberfläche → Backend → BamBuddy-Inventar:**

- **Automatisch (BamBuddy fragen):** wie bisher. Darunter steht, was gerade gilt, oder in Rot „BamBuddy verweigert die Abfrage (HTTP 403)“.
- **Spoolman (über BamBuddy)** und **BamBuddys eigenes Inventar:** gelten nur, solange BamBuddy die Abfrage verweigert. Antwortet BamBuddy, folgt die Waage BamBuddy, und die Seite sagt das. Sonst hätte die Waage eine Spule aus dem einen Inventar angezeigt und ihr Gewicht über BamBuddys Gerätefunktionen in das andere geschrieben.

filament_to_bambuddy hat denselben Schalter, unten auf seiner Seite, mit derselben Regel. Am einfachsten: dem Schlüssel „Read Status“ geben und beide auf **Automatisch** lassen.

**Spoolman-Adresse für die Waage:** Für einen Box-Scan sucht die Waage direkt in Spoolman nach vorhandenen Spulen, unter der Adresse, die BamBuddy für seinen Spoolman kennt. Läuft beides in Docker, ist das oft ein Name wie `http://spoolman:8000`, den nur BamBuddy erreicht. Jeder Box-Scan endete dann mit „Suche fehlgeschlagen (-1)“. In diesem Fall unter **Backend → Spoolman-Adresse für die Waage** die Adresse eintragen, unter der die Waage Spoolman erreicht (z. B. `http://192.168.1.5:7912`). Leer bleibt BamBuddys Adresse.

**Deine Spoolman-Filamente:** Im Spoolman-Modus sucht filament_to_bambuddy zu einem gescannten Code zuerst in deinen eigenen Spoolman-Filamenten (Artikelnummer, sonst Marke, Material und Farbe). Findet es eines, legt die Waage die neue Spule genau auf diesem Filament an, mit seinen Temperaturen, seinem Leergewicht und seinem Preis, statt BamBuddy ein neues, fast leeres Filament machen zu lassen. Ist das Filament inzwischen gelöscht, legt sie die Spule wie bisher an.

Was die Waage beim Scannen jetzt außerdem kann:

- **QR-Codes von BamBuddy-Etiketten** (`…/inventory?spool=12`), von den Etiketten der Waage selbst und von filament_to_bambuddy laden die Spule. Bisher kam „unbekannter Code“.
- **UPC-Codes mit führender Null** (Hatchbox, Overture, Inland …) findet die Waage wieder. filament_to_bambuddy speichert sie ohne die Null, die Waage verglich bisher genau und bot jedes Mal eine neue Spule an.
- **Nachkauf:** Ist die letzte Spule eines Produkts aufgebraucht, bietet der Scan einer neuen Box eine neue Spule auf demselben Filament an. Bisher kam nur „keine aktive Spule“.
- **Leergewicht:** Kommt es vom Spoolman-Filament, legt die Waage es nicht mehr auf jede neue Spule. Die Spule erbt es vom Filament, eine Korrektur dort gilt dann für alle.
- **Druckprofil:** Das passende in BamBuddy importierte Profil (siehe filament_to_bambuddy, *Druckprofile*) kommt mit auf die neue Spule. Die Profile für weitere Druckermodelle ergänzt filament_to_bambuddy über **Dein Bestand → Druckprofile zuordnen**.
- **Leergewicht einer Spule ändern** geht jetzt auch mit BamBuddy + Spoolman. BamBuddy speichert es dort seit #2908.
- Die Anfrage an filament_to_bambuddy darf 30 s dauern statt 15 s und lässt die Preissuche weg (`quick=1`).

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
