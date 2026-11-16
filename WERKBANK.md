# SpoolmanScale als Werkbank-Terminal: Anleitung (deutsch)

Dieser Fork erweitert die SpoolmanScale um:

- einen Barcode-Scanner,
- die Drucker-Steckdosen,
- eine Abfrage nach jedem Druck,
- das Anlegen neuer Spulen über [filament_to_bambuddy](https://github.com/AreYouThereAndi2/filament_to_bambuddy), auf Wunsch mit Preis.

Ausgelegt ist er auf **BamBuddy mit Spoolman dahinter**. Der Scanner funktioniert auch mit Spoolman direkt.

Die Anleitung hat drei Teile:

| Teil | Inhalt |
|---|---|
| [**1 Aufbau**](werkbank/1-aufbau.md) | Vorbereitung, Teile, Pin-Plan, Aufbau in Stufen mit Test, Firmware, Ersteinrichtung, Funktionstest |
| [**2 Bedienung**](werkbank/2-bedienung.md) | Gesten und Schnellmenü, Weboberfläche (Waage, Nullpunkt, Sicherung), Inventar, Zubehör, Etikettendrucker |
| [**3 Wartung**](werkbank/3-wartung.md) | Updates, Fehlersuche, Release machen, offene Punkte |

**Neu hier?** Mit [Bevor du anfängst](werkbank/1-aufbau.md#bevor-du-anfängst) beginnen.

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
| Etiketten auf dem TYPONOS PM240 über die Druckbrücke (experimentell) | Einstellungen → Bluetooth → Drucker; Adresse in der Weboberfläche → Drucker | alle |
| Zubehör an der Spule (Clip, RFID-Halter) zusätzlich abziehen | Weboberfläche → Einstellungen → Zubehör an der Spule; je Spule im Wiegen-Dialog | alle |
| HX711 oder NAU7802 als Wägezellen-Verstärker, wird selbst erkannt | automatisch beim Start | alle |
| Bedienen wie am Handy: vom Rand wischen, Schnellmenü von oben | überall | alle |
| Waage live (Rohwert, Streuung, Kalibrierung) und Nullpunkt nachführen | Weboberfläche → Waage | alle |
| Alle Einstellungen sichern und wiederherstellen | Weboberfläche → Einstellungen → Sichern und wiederherstellen | alle |
| Neue Spulen über filament_to_bambuddy, Etiketten im Handy-Layout | Adresse von filament_to_bambuddy eintragen | BamBuddy |

Alles bezieht sich auf den Drucker, der in der AMS-Ansicht gewählt ist (Chip „1/2 P1S“): der Power-Button, die Abfrage nach dem Druck und das Abschalten.

## Etikettendrucker PM240 über die Druckbrücke

Der TYPONOS PM240 druckt nur über klassisches Bluetooth (SPP), das hat ein Mitschnitt der Hersteller-App gezeigt. Über Bluetooth LE nimmt er Aufträge an, druckt aber nichts. Der ESP32-S3 der Waage kann kein klassisches Bluetooth. Darum steht neben dem Drucker eine **Druckbrücke**: ein zweiter, gewöhnlicher ESP32, der den Auftrag über das WLAN annimmt und per SPP weitergibt. Bauen und einrichten: [bridge/README.md](bridge/README.md).

An der Waage: Einstellungen → Bluetooth → **Drucker** → Modell **PM240 (WLAN-Brücke)** (Bluetooth darf aus sein). Die Adresse der Brücke (`printbridge.local` oder IP) und ein eventueller API-Schlüssel kommen in der Weboberfläche unter **Drucker** dazu, dort gibt es auch „Verbindung prüfen“ und den Testdruck. Einzelheiten und Meldungen: [Bedienung → Etikettendrucker](werkbank/2-bedienung.md#etikettendrucker-typonos-pm240-über-die-druckbrücke).
