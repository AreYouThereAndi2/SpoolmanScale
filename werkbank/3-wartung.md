# Werkbank 3: Wartung

[Übersicht](../WERKBANK.md) · [1 Aufbau](1-aufbau.md) · [2 Bedienung](2-bedienung.md) · [3 Wartung](3-wartung.md)

Updates, Fehlersuche, Releases und offene Punkte.

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

## Fehlersuche: was ich zum Helfen brauche

Die meisten Probleme zeigt die Waage selbst an. Die Tabellen „Wenn es nicht geht“ stehen bei den [Stufen im Aufbau](1-aufbau.md#aufbau-in-stufen).

Hilft das nicht, bitte schicken:

1. **Was du getan hast und was passiert ist.** Ein Foto vom Bildschirm hilft.
2. **Das Log:** Weboberfläche → **Log** → herunterladen, oder die Datei von der SD-Karte, falls eine steckt.
3. **Die Werte der Waage:** Weboberfläche → **Waage**. Sie zeigen Wandler, Rohwert, Streuung und Kalibrierung.
4. Bei Verdrahtungsfragen ein Foto der Lötstellen und des I/O-Kabels.

Dazu passend: Weboberfläche → Einstellungen → **Sichern und wiederherstellen**. Vor größeren Änderungen eine Sicherung ziehen, dann ist nach einem Neuflashen alles in einer Minute zurück.

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

## Noch offen

- **Hardware-Test:** Bisher ist alles nur kompiliert und mit den Prüfskripten getestet, noch nicht auf dem Gerät.
