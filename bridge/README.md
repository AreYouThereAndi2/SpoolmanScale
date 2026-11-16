# Druckbrücke

Ein kleines ESP32-Board neben dem Etikettendrucker. Es nimmt Druckaufträge über das WLAN an und gibt sie per Bluetooth Classic an einen **TYPONOS PM240** weiter.

> [!WARNING]
> Version 0.1.0 ist gebaut, aber noch nicht an einem echten Drucker getestet. Rückmeldungen mit dem seriellen Log (115200 Baud) helfen.

## Was und warum

Der PM240 druckt nur über **Bluetooth Classic (SPP)**. Das hat ein Mitschnitt der Hersteller-App Labelife gezeigt: Sie öffnet eine serielle Bluetooth-Verbindung (RFCOMM, Dienst „JL_SPP“) und schickt reinen TSPL-Text. Der Drucker bietet zwar auch Bluetooth Low Energy an, druckt darüber aber nichts.

Die SpoolmanScale (ESP32-S3) und Browser (Web Bluetooth) können nur Bluetooth Low Energy. Die Druckbrücke schließt diese Lücke:

```
Waage / filament_to_bambuddy / CUPS  --WLAN-->  Druckbrücke (ESP32)  --Bluetooth Classic-->  PM240
```

Sie leitet die Bytes nur weiter. Das Etikett selbst (TSPL mit Bitmap) baut weiterhin der Absender.

## Was kaufen

- **ESP32-WROOM-32 DevKit** (das „originale“ ESP32, z. B. „ESP32 DevKitC V4“ oder „ESP32-WROOM-32 NodeMCU“), am besten mit USB-C, 4 MB Flash. PSRAM ist nicht nötig.
- **Nicht** ESP32-S3, ESP32-C3, ESP32-C6 oder ESP32-H2: Diese Chips haben kein Bluetooth Classic, die Druckbrücke läuft darauf nicht.
- Ein USB-Netzteil (5 V, 1 A reicht) und ein Kabel. Ein Gehäuse ist optional.

Das Board steht am besten in Bluetooth-Reichweite des Druckers (wenige Meter) und im WLAN-Empfang.

## Flashen

**Mit esptool** (Image aus dem GitHub-Release, Tag `bridge-v…`):

```
esptool.py --chip esp32 --baud 460800 write_flash 0x0 Druckbruecke.bin
```

`Druckbruecke.bin` ist das komplette Image (Bootloader, Partitionstabelle, Anwendung) und gehört an Adresse **0x0**.

**Selbst bauen** (PlatformIO 6.2 oder neuer):

```
pio run -d bridge -t upload
pio device monitor -b 115200
```

**Im Browser** (ESP Web Tools): `bridge/manifest.json` ist dafür vorbereitet. Eine eigene Flasher-Seite gibt es noch nicht (siehe [Offen](#offen)).

Manche Boards gehen nur in den Flash-Modus, wenn man beim Verbinden die Taste **BOOT** gedrückt hält.

## WLAN einrichten

Drei Wege, der erste passende genügt:

1. **Direkt nach dem Flashen im Browser:** ESP Web Tools fragt über die USB-Verbindung nach dem WLAN (Improv Serial). Netzwerk wählen, Passwort eingeben, fertig. Danach zeigt der Flasher den Link zur Weboberfläche.
2. **Einrichtungs-WLAN:** Ohne gespeichertes WLAN öffnet die Druckbrücke ein offenes WLAN **`Druckbruecke-XXXX`**. Mit dem Handy verbinden; die Einrichtungsseite öffnet sich meist von selbst, sonst `http://192.168.4.1/` aufrufen. Unter **WLAN** „Netzwerke suchen“, Netz antippen, Passwort eingeben, „Verbinden“. Die neue Adresse erscheint oben unter **Status**.
3. **Später ändern:** Auf der Weboberfläche unter **WLAN**.

Klappt die Verbindung 30 Sekunden lang nicht (Passwort geändert, Router getauscht), geht das Einrichtungs-WLAN zusätzlich wieder auf. Die Druckbrücke versucht es weiter mit dem gespeicherten Netz, solange niemand im Einrichtungs-WLAN ist.

Danach ist sie unter **`http://printbridge.local/`** erreichbar (oder über die IP-Adresse aus dem Router). Der Name lässt sich unter **Einstellungen** ändern.

## PM240 koppeln

1. Drucker einschalten und neben die Druckbrücke stellen. Die Hersteller-App am Handy schließen oder Bluetooth am Handy ausschalten: Der Drucker nimmt nur eine Verbindung an.
2. Weboberfläche öffnen, unter **Drucker wählen** auf „Drucker suchen“ tippen (dauert etwa 10 Sekunden).
3. Den PM240 in der Liste antippen. Die **PIN** bleibt `0000`, wenn der Drucker keine andere verlangt. „Speichern“.
4. „Testetikett drucken“. Die Druckbrücke verbindet sich, koppelt beim ersten Mal und druckt ein Etikett mit „Druckbruecke OK“, IP-Adresse und Hostname.

Die Verbindung wird bei jedem Auftrag aufgebaut und nach einer Minute ohne Auftrag wieder getrennt, damit der Akku des Druckers geschont wird. Der erste Auftrag danach braucht deshalb ein paar Sekunden länger.

„Drucker vergessen“ löscht Adresse und Kopplung.

## Wer sie benutzt

- **SpoolmanScale:** Druckermodell „PM240 über WLAN-Brücke“ wählen, als Adresse `printbridge.local` (oder die IP) und gegebenenfalls den API-Schlüssel eintragen. Die Waage baut das Etikett wie bisher und schickt es an `/api/print`.
- **filament_to_bambuddy:** Die Seite läuft über https und darf die Druckbrücke (http) nicht direkt aufrufen. Der Server von filament_to_bambuddy reicht die Aufträge durch (`POST /api/print/raw`, `GET /api/print/status`). Die Adresse kommt aus `PRINT_BRIDGE_URL` (z. B. `http://printbridge.local`), der Schlüssel aus `PRINT_BRIDGE_KEY`; beides lässt sich in den Einstellungen der Seite unter *Mehr* überschreiben.
- **CUPS und andere Programme:** Rohdaten an TCP-Port **9100** (JetDirect / AppSocket), z. B. `socket://printbridge.local:9100`. Was bis zum Schließen der Verbindung kommt, ist ein Auftrag.

## API

HTTP auf Port 80, Antworten als JSON, jede Antwort mit `Access-Control-Allow-Origin: *`. Ist auf der Weboberfläche ein **API-Schlüssel** gesetzt, brauchen alle Aufrufe unter `/api/` außer `GET /api/status`, und ebenso `/update`, den Header `X-Api-Key: <Schlüssel>`, sonst kommt `401 {"ok":false,"error":"api key"}`.

| Aufruf | Zweck | Antwort |
|---|---|---|
| `GET /api/status` | Zustand | siehe unten |
| `POST /api/print` | Body = Druckauftrag (TSPL-Bytes), `Content-Type: application/octet-stream`, höchstens 256 KiB, `Content-Length` nötig | `200 {"ok":true,"bytes":N,"ms":T}` |
| `POST /api/test` | eingebautes Testetikett | wie `/api/print` |
| `GET /api/scan` | Bluetooth-Suche, etwa 10 s | `{"ok":true,"devices":[{"address":"80:52:D3:F8:B9:BC","name":"PM240","rssi":-50}]}` |
| `POST /api/printer` | JSON `{"address":"80:52:D3:F8:B9:BC","name":"PM240","pin":"0000"}` speichert, `{"address":""}` vergisst | `{"ok":true}` |
| `POST /api/settings` | JSON `{"api_key":"…","hostname":"printbridge"}`, fehlende Felder bleiben, `"api_key":""` entfernt den Schlüssel | `{"ok":true}` |
| `GET /api/wifi/scan` | WLAN-Netze in der Nähe | `{"ok":true,"networks":[{"ssid":"…","rssi":-60,"open":false}]}` |
| `POST /api/wifi` | JSON `{"ssid":"…","pass":"…"}` speichert und verbindet | `{"ok":true}` |
| `POST /update` | Firmware (`Druckbruecke_app.bin`) als multipart-Upload, startet danach neu | `{"ok":true}` |

Fehler beim Drucken:

| Status | `error` | Bedeutung |
|---|---|---|
| 400 | `empty` | kein Body (oder ohne `Content-Length`) |
| 409 | `busy` | ein anderer Auftrag oder eine Suche läuft |
| 412 | `no printer` | kein Drucker gewählt |
| 413 | `too large` | mehr als 256 KiB |
| 502 | `connect failed` | Drucker nicht erreichbar (aus, zu weit weg, mit dem Handy verbunden, falsche PIN) |
| 502 | `write failed` | Verbindung abgebrochen oder nach 30 s nicht fertig |

Ein Auftrag wird beim Empfang gleich weitergeschickt; die Antwort kommt, wenn alle Bytes abgeschickt sind. Das Verbinden dauert normalerweise wenige Sekunden, im schlechtesten Fall (Drucker antwortet, Kopplung klappt nicht) bis zu 30 Sekunden; das Senden höchstens 30 Sekunden. Absender sollten also bis zu 70 Sekunden auf die Antwort warten.

Beispiel `GET /api/status`:

```json
{"ok":true,"name":"Druckbrücke","version":"0.1.0",
 "printer":{"address":"80:52:D3:F8:B9:BC","name":"PM240","paired":true,"connected":false,"battery":null},
 "busy":false,"jobs":12,
 "last":{"ok":true,"bytes":11622,"ms":2300,"error":"","at":5321},
 "hostname":"printbridge","auth":false,"uptime":5400,
 "wifi":{"ssid":"Heimnetz","ip":"192.168.0.50","rssi":-58,"ap":false,"ap_name":""},
 "heap":81234}
```

- `address` ist leer, solange kein Drucker gewählt ist. `battery` ist eine Prozentzahl oder `null`, solange der Drucker sie nicht verraten hat.
- `jobs` zählt die erfolgreichen Aufträge seit dem Start, `last.at` ist die Laufzeit in Sekunden beim letzten Auftrag (vergleichbar mit `uptime`), `last` ist `null` vor dem ersten Auftrag.
- `hostname`, `auth`, `uptime`, `wifi` und `heap` sind Zusätze für die Weboberfläche.

Beispiel mit curl:

```
curl -H "X-Api-Key: geheim" --data-binary @etikett.tspl \
     -H "Content-Type: application/octet-stream" http://printbridge.local/api/print
```

## Update

Weboberfläche → **Firmware aktualisieren** → `Druckbruecke_app.bin` aus dem Release wählen → „Hochladen“. Die Druckbrücke startet danach neu; WLAN, Drucker und Einstellungen bleiben erhalten. (`Druckbruecke.bin` ist das Image zum Erst-Flashen und wird hier abgelehnt.)

## Fehlersuche

Das serielle Log (USB, 115200 Baud) zeigt jeden Schritt: WLAN, Bluetooth-Verbindung, gefundene Dienste des Druckers, jeden Auftrag mit Bytes und Dauer, und alles, was der Drucker zurückschickt (als Hex).

- **„connect failed“:** Drucker an? Nicht mit dem Handy verbunden? Näher heranstellen. Bei Kopplungsproblemen „Drucker vergessen“ und neu wählen.
- **Seite nicht erreichbar unter `.local`:** Manche Android-Geräte und Windows ohne Bonjour lösen `.local` nicht auf; dann die IP-Adresse nehmen.
- **Schlüssel vergessen:** Neu flashen mit „Gerät löschen“ setzt alles zurück.

## Technik

- PlatformIO-Projekt in diesem Ordner, Umgebung `esp32dev`, Arduino-Core 3.3.12 (pioarduino, wie die Waage). Partitionen: `min_spiffs.csv`, zwei App-Slots à 1,9 MB für Updates.
- Bluetooth: `BluetoothSerial` als SPP-Master, BLE abgeschaltet (spart Speicher). Kanal per SDP (bevorzugt der Dienst mit „SPP“ im Namen), zuerst mit Authentifizierung und PIN, sonst ohne.
- Akku: Nach dem Verbinden schickt die Druckbrücke `BATTERY?` und wertet eine Antwort `BATTERY ` + ein Byte (0 bis 100) aus. Das Format ist nur halb bekannt; alles andere ergibt `null`. Ein Auftrag wartet nie darauf.
- Ein Auftrag zur Zeit (HTTP und Port 9100 teilen sich die Verbindung). Der Body wird nicht zwischengespeichert, sondern in Stücken direkt weitergegeben: Das Board hat kein PSRAM.

## Offen

- Noch nicht an einem echten PM240 getestet: Kopplung (PIN oder „just works“), welcher SDP-Kanal, Durchsatz, Format der Akku-Antwort.
- Das Testetikett nutzt die eingebauten Schriften des Druckers, deshalb steht „Druckbruecke“ ohne Umlaut darauf.
- Flasher-Seite im Browser: `manifest.json` erwartet das Image unter `bridge/webflasher/Druckbruecke.bin` auf GitHub Pages; der Workflow legt es bisher nur ins Release.
