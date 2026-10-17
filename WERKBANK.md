# SpoolmanScale als Werkbank-Terminal (Fork)

Dieser Fork erweitert die SpoolmanScale um einen Barcode-Scanner, die Steuerung der Drucker-Steckdosen, eine Abfrage nach jedem Druck und das Anlegen neuer Spulen über [filament_to_bambuddy](https://github.com/AreYouThereAndi2/filament_to_bambuddy). Ausgelegt ist er auf **BamBuddy mit Spoolman dahinter**. Der Scanner funktioniert auch mit Spoolman direkt.

## Was dazugekommen ist

| Funktion | Wo | Backend |
|---|---|---|
| Barcode-Scanner (GM861, UART) | Weboberfläche → Einstellungen → Barcode-Scanner | alle; EAN-Suche braucht Spoolman |
| Spoolman-QR scannen → Spule laden, wiegen, danach AMS-Slot wählen | automatisch | alle; AMS-Slot nur BamBuddy |
| Unbekannten Tag per QR-Scan verknüpfen | Scan, während der Tag aufliegt | alle |
| EAN / Hersteller- / Amazon-Nummer → neue Spule anlegen | Scan, Adresse von filament_to_bambuddy eintragen | BamBuddy |
| Power-Button pro Drucker | AMS-Ansicht, links neben dem Titel | BamBuddy |
| Abfrage nach dem Druck: erfolgreich? Spule leer? ausschalten? | automatisch; Schalter „Nach dem Druck fragen“ in den BamBuddy-Optionen | BamBuddy |
| Spule aus AMS-Slot austragen | AMS-Ansicht → Slot antippen → „Austragen“ (zweimal tippen) | BamBuddy |

Der Drucker, der in der AMS-Ansicht gewählt ist (Chip „1/2 P1S“), gilt für alles: Power-Button, Abfrage nach dem Druck und Abschalten.

## Was im Gemini-Handoff nicht stimmte

- **Pins:** GPIO 5/6 ist der interne Touch-Bus. Waage und NFC hängen an **GPIO 10/11**. Der Scanner kommt an **GPIO 13 (Pin 6, weiß)**.
- **DIP-Schalter am roten PN532:** Für I2C ist **1 = ON, 2 = OFF** richtig. „1 = 0, 2 = 1“ stellt SPI ein.
- **Versorgung:** Der Anschluss liefert nur 5 V und GND. Den TX-Pegel des Scanners vorher messen, er muss bei 3,3 V liegen.
- **Spoolman `PATCH gross_weight`** gibt es nicht. Gewogen wird über `PUT /spool/{id}/measure`, so wie es die Firmware immer schon macht.
- **BamBuddy-Endpunkte:** `/printers/{id}/power`, `safe_cooldown`, `/prints/{id}/finish` und `/printers/{id}/clear_print` gibt es nicht. Verwendet werden, geprüft am BamBuddy-Quellcode 1.2.5.7:
  - `GET /smart-plugs/by-printer/{id}`, `GET /smart-plugs/{id}/status`, `POST /smart-plugs/{id}/control` mit `{"action":"on"|"off"}`
  - `GET /printers/{id}/status` (Temperaturen, Zustand)
  - `GET /archives/?printer_id=X&limit=1` und `PATCH /archives/{id}` mit `user_verdict` `good`/`reject`
  - AMS: `/spoolman/inventory/slot-assignments` (war schon in der Firmware)

## Verdrahtung Scanner

| I/O-Pin | Signal | Scanner |
|---|---|---|
| 1 | 5 V | VCC, nur wenn das Handbuch 5 V erlaubt |
| 2 | GND | GND |
| 6 (weiß) | GPIO 13 | TX des Scanners |
| 8 | GPIO 21 | RX des Scanners (optional, nicht am 7-poligen Kabel) |

Standard sind 9600 Baud. Pins und Baudrate lassen sich in der Weboberfläche ändern. GPIO 12 und 14 stehen zur Wahl, sind aber bei manchen Geräten der Reset des PN532.

## Einrichtung

1. **Einmal per USB flashen.** Die Firmware ist größer als 3 MB und passt nicht in die alte Partitionstabelle. Über den Web Flasher („Install“) oder `pio run -t upload` bekommt die Waage die 6-MB-Tabelle aus `partitions.csv`. Ein OTA-Update aus dem Original-Repo würde diese Funktionen wieder entfernen.
2. **BamBuddy-API-Schlüssel:** Zusätzlich zu „Read Status“ und „Manage Inventory“ braucht er **„Control Printer“** (Steckdosen) und **„Manage Archives“** (Bewertung nach dem Druck).
3. **Steckdose in BamBuddy** dem Drucker zuordnen (Settings → Smart Plugs). Ist dort „Auto Power Off“ an, schaltet BamBuddy selbst ab, und die Waage sagt das nur. Sonst wartet die Waage, bis Düse und Bett unter der eingestellten Temperatur liegen (Standard 50 °C).
4. **Scanner einschalten:** Weboberfläche → Einstellungen → Barcode-Scanner. Dort auch die **filament_to_bambuddy-Adresse** eintragen, z. B. `http://zimaboard:8088`.
5. **AMS-Abfrage nach dem Wiegen** einschalten: BamBuddy-Optionen → „Nach dem Wiegen ins AMS“.

## Updates aus diesem Fork

Die Waage sucht Updates jetzt in `AreYouThereAndi2/SpoolmanScale`:

- Versionsdatei: `https://areyouthereandi2.github.io/SpoolmanScale/version.json`. Dafür **GitHub Pages** aktivieren: Settings → Pages → Branch `main`, Ordner `/`.
- Release: `FW_VERSION` in `src/app_config.h` und die Versionszeile in `src/main.cpp` erhöhen, dann einen Tag mit genau dieser Version pushen. Der Workflow „Build and Release“ baut das Image, legt das Release an und schreibt `version.json` auf `main`.

## Noch offen

- **Etikettendruck TYPONOS PM240:** Das Protokoll ist unbekannt. Gebraucht wird ein Bluetooth-Mitschnitt: Android → Entwickleroptionen → „Bluetooth-HCI-Snoop-Protokoll“, dann aus der App ein Testlabel drucken.
- **Hardware-Test:** Alles ist nur kompiliert und mit den Prüfskripten getestet, noch nicht auf dem Gerät.
