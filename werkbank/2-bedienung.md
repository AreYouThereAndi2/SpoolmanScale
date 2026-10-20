# Werkbank 2: Bedienung

[Übersicht](../WERKBANK.md) · [1 Aufbau](1-aufbau.md) · [2 Bedienung](2-bedienung.md) · [3 Wartung](3-wartung.md)

Die Waage im Alltag: Gesten, Weboberfläche, Inventar, Zubehör und Etiketten.

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

## Weboberfläche: Waage, Nullpunkt, Sicherung

**Waage** (eigener Reiter, nur wenn eine Waage eingebaut ist):

- Zeigt live, welcher Wandler gefunden wurde (HX711 oder NAU7802), den Rohwert, das Gewicht, die Streuung und den Kalibrierfaktor.
- Ideal für den ersten Test nach dem Löten: leicht auf die Schale drücken, Rohwert und Gewicht müssen **steigen**. Eine Streuung unter 1 g bei leerer Schale ist gut. Darüber hat meist eine Ader Wackelkontakt.
- **Nullpunkt nachführen** (ab Werk an): Liegt nichts auf der Schale und ist sie ruhig, holt die Waage kleine Abweichungen bis 2 g nach 10 Sekunden auf 0 g zurück, etwa wegen der Temperatur. Eine aufgelegte Spule wird nie wegtariert. Gespeichert wird höchstens alle 10 Minuten, das schont den Flash.

**Einstellungen → Sichern und wiederherstellen:**

- **Einstellungen herunterladen** speichert alle Einstellungen als eine Datei: Kalibrierung, Backend, Zubehör, Anzeige, Scanner, Drucker.
- WLAN-Passwort und API-Schlüssel kommen nur mit, wenn das Häkchen gesetzt ist. Die Datei dann sicher aufbewahren.
- **Einspielen** schreibt die Datei zurück, danach startet die Waage neu. Gerätewerte wie die Firmware-Version oder ob ein HX711 gefunden wurde, misst die Waage selbst neu.

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
- **filament_to_bambuddy legt die Spule an**, genau wie vom Handy. Sie kommt auf dein Spoolman-Filament und bekommt das Druckprofil für jedes deiner Druckermodelle. Betttemperatur, Preis und Artikelnummer landen auf dem Filament, der Lagerort aus dem Speicher, und der Code wird gemerkt. Ist filament_to_bambuddy nicht erreichbar, legt die Waage die Spule wie bisher selbst über BamBuddy an.
- **Etiketten im Handy-Layout:** Druckt die Waage ein Etikett, holt sie es von filament_to_bambuddy, in der Anordnung, die du am Handy eingestellt hast (siehe dort, *Etiketten drucken*). Ohne filament_to_bambuddy druckt sie ihr eigenes Etikett.
- **Klare Fehlermeldungen:** Statt „fehlgeschlagen (-1)“ steht da, was los ist, z. B. „filament_to_bambuddy nicht erreichbar“, „Spoolman nicht erreichbar, Adresse prüfen“, „API-Schlüssel: Recht fehlt“, „abgelehnt, Inventar-Modus prüfen“. Die Zahl dahinter bleibt für die Fehlersuche.
- **Leergewicht einer Spule ändern** geht jetzt auch mit BamBuddy + Spoolman. BamBuddy speichert es dort seit #2908.
- Die Anfrage an filament_to_bambuddy darf 30 s dauern statt 15 s. Der Durchschnittspreis aus den Shop-Angeboten kommt wieder mit.

## Zubehör an der Spule abziehen (Clip, RFID-Halter)

Die Waage kennt das Leergewicht der Spule aus FilaMan, Spoolman oder BamBuddy. Ein Filament-Clip oder ein RFID-Halter an der Spule wiegt zusätzlich ein paar Gramm, und die zieht die Waage auf Wunsch ebenfalls ab.

1. **Weboberfläche → Einstellungen → Zubehör an der Spule:** Bis zu 8 Teile mit Namen und Gewicht eintragen, z. B. „Clip 1“ = 2 g, „Clip 2“ = 5 g, „RFID-Halter 1“ = 3 g. Bis zu **3** davon als **Standard** ankreuzen, dann **Speichern**. Ein leerer Name gibt den Platz frei.
2. **Der Standard** gilt für jede Spule, für die nichts anderes gewählt ist.
3. **Je Spule:** Im Wiegen-Dialog steht oben rechts z. B. `✎ -5 g`. Antippen öffnet die Auswahl: bis zu 3 Teile antippen, dann **OK**. Die Waage merkt sich die Auswahl für diese Spule und zeigt den Dialog mit den neuen Zahlen wieder an.

Abgezogen wird überall, wo aus der Anzeige ein Filamentgewicht wird: Netto auf dem Hauptbildschirm, die Knöpfe im Wiegen-Dialog, automatisches Wiegen, die AMS-Abfrage, die Spulenwahl nach Gewicht und **Leerspule wiegen**. Wird also eine leere Spule mit Clip gewogen, landet nur die Spule als Leergewicht im Backend. Die Brutto-Angabe an FilaMan ist das Gewicht ohne Zubehör, so wie beim Beutel.

Der Abzug ist eine Einstellung der Waage. Er liegt nicht im Backend: Eine zweite Waage oder BamBuddy selbst wissen nichts davon.

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
