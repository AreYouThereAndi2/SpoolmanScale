# Werkbank 1: Aufbau

[Übersicht](../WERKBANK.md) · [1 Aufbau](1-aufbau.md) · [2 Bedienung](2-bedienung.md) · [3 Wartung](3-wartung.md)

Vom Auspacken bis zur fertig kalibrierten Waage, Stufe für Stufe. Danach geht es in [2 Bedienung](2-bedienung.md) weiter.

## Bevor du anfängst

Eine Stunde Vorbereitung spart später die Fehlersuche. Alles abhaken, bevor der Lötkolben heiß wird:

**Teile bereitlegen**

- [ ] WT32-SC01 Plus mit dem 7-poligen I/O-Kabel
- [ ] HX711-Platine und Wägezelle (oder NAU7802)
- [ ] 3,3-V-Regler mit **festen** 3,3 V (AMS1117-3.3-Modul, z. B. Fasizi „4,75–12 V auf 3,3 V, 800 mA“), für HX711 und Scanner, siehe [Der 3,3-V-Regler](#der-33-v-regler)
- [ ] PN532 V3, DIP-Schalter schon auf **1 = ON, 2 = OFF**
- [ ] optional: GM861-Scanner, USB-C-Einbaukabel, NTAG215-Aufkleber, PM240
- [ ] ein USB-C-**Datenkabel** (mit einem reinen Ladekabel klappt das Flashen nicht)
- [ ] ein bekanntes Gewicht für die Kalibrierung, z. B. eine volle 1-l-Flasche, auf der Küchenwaage gewogen

**Werkzeug bereitlegen**

- [ ] Lötkolben mit feiner Spitze, Lötzinn, Seitenschneider, Abisolierzange
- [ ] **Multimeter**: damit misst du vor jedem neuen Teil die Spannung
- [ ] Schrumpfschlauch oder Kapton-Band
- [ ] dünne Litze in mehreren Farben, je 30 bis 40 cm

**Vorher erledigen**

- [ ] **Firmware zuerst flashen**, noch ohne etwas anzuschließen ([Stufe 0](#stufe-0-nur-das-display-ohne-kabel)). Dann weißt du, dass Display, USB-Kabel und WLAN gehen, bevor du lötest.
- [ ] Den [Pin-Plan](#so-hängt-alles-zusammen) ausdrucken oder auf dem Handy offen haben.
- [ ] Die Pins am I/O-Kabel mit Klebeband beschriften (1 bis 7). Die Farben der Adern sind bei Nachbauten nicht immer gleich.
- [ ] Ein Foto vom Kabel und von jeder Platine machen, bevor du lötest. Bei der Fehlersuche hilft das enorm.
- [ ] Die Weboberfläche der Waage im Browser öffnen: unter **Waage** siehst du nach dem Löten sofort, ob der Wandler antwortet.

**Faustregeln beim Löten**

1. Nur stromlos arbeiten: USB ab, bevor du etwas an- oder absteckst.
2. Nach jeder Stufe testen, nicht alles auf einmal.
3. Neue Spannung immer zuerst ohne Verbraucher messen, z. B. den 3,3-V-Regler.
4. Jede Lötstelle einzeln isolieren.

## Teile und Werkzeug

Die vollständige Teileliste mit Links steht in [BUILDING.md](../BUILDING.md#parts-list). Diese Anleitung geht von den Teilen in der Tabelle aus. Für die Waage gibt es zwei Wege: **HX711** (günstig, im Set mit der Wägezelle) oder **NAU7802** (Adafruit). Die Firmware erkennt beide von selbst.

| Teil | Modell | Für Stufe |
|---|---|---|
| Display + Rechner | WT32-SC01 Plus (ESP32-S3, 3,5 Zoll, mit 7-poligem I/O-Kabel) | 0 |
| USB-C-Kabel | ein **Datenkabel**, kein reines Ladekabel | 0 |
| 3,3-V-Spannungsregler | Modul mit festen 3,3 V: AMS1117-3.3 (z. B. Fasizi, 4,75–12 V Eingang, 800 mA) oder AP2112-3.3 | 1 (HX711), 4 |
| Waagen-ADC | **HX711**-Platine (im Set mit der Wägezelle) *oder* NAU7802 | 1 |
| Wägezelle | Biegebalken 5 oder 10 kg (10 kg reicht bis zu sehr schweren Spulen) | 1 |
| NFC-Leser | PN532 „NFC Module V3“ (rote Platine mit zwei DIP-Schaltern) | 2 |
| Gehäuse, Schrauben | siehe BUILDING.md | 3 |
| USB-C-Einbaukabel | USB-C Stecker auf Einbaubuchse, ca. 30 cm | 3 |
| Barcode-Scanner (optional) | GM861, auch im Edelstahlgehäuse mit UART/USB-Kabel | 4 |
| Etikettendrucker (optional) | TYPONOS PM240, Bluetooth, **ohne Kabel** | 5 |
| NFC-Aufkleber | NTAG215, 25 mm (NTAG213 geht auch, hat aber weniger Platz) | ab 2 |

**Werkzeug:** Lötkolben mit feiner Spitze, Lötzinn, Abisolierzange, Seitenschneider, **Multimeter**, Schrumpfschlauch oder Kapton-Band. Dazu dünne Litze in mehreren Farben (je 30 bis 40 cm).

## So hängt alles zusammen

Alles hängt an **einer Buchse**: dem **I/O-Stecker** auf der Rückseite des WT32-SC01 Plus. Dort steckt das mitgelieferte **7-polige Kabel**, **links bündig** in der 8-poligen Buchse. Der achte Kontakt (ganz rechts) bleibt frei, er wird nur für eine Option beim Scanner gebraucht.

Die Buchse hat **nur 5 V**, keine 3,3 V. Was 3,3 V braucht (HX711, GM861), hängt an einem kleinen **3,3-V-Regler**, den du an Pin 1 und 2 anschließt.

### Der 3,3-V-Regler

Ein kleines Modul mit **festen 3,3 V**, z. B. das AMS1117-3.3-Modul von Fasizi („4,75–12 V auf 3,3 V, 800 mA“, 10 Stück). Ein Regler versorgt HX711 und Scanner zusammen.

| Regler | geht an |
|---|---|
| **IN** (auch VIN) | Pin 1, rot (5 V) |
| **GND** | Pin 2, schwarz |
| **OUT** (auch 3V3, VOUT) | HX711 VCC (und VDD), GM861 VCC |

- **IN und OUT nicht vertauschen.** Die Beschriftung steht auf der Platine. Hat das Modul GND auf beiden Seiten, reicht ein GND-Draht.
- **Vor dem ersten Verbraucher messen:** nur IN und GND anschließen, USB an, OUT gegen GND messen. Es müssen **3,25 bis 3,35 V** sein (3,2 bis 3,4 V sind auch in Ordnung). Erst dann HX711 und Scanner anschließen.
- **Warum fest und linear:** Es gibt kein Poti, das sich verstellen kann, und kein Schaltrauschen. Rauschen auf der Versorgung würde der HX711 als zappelndes Gewicht anzeigen.
- **Last:** Scanner höchstens 70 mA, HX711 etwa 1,5 mA, zusammen rund 75 mA. Der Regler schafft 800 mA und wird nur handwarm. Einen Kühlkörper braucht er nicht.
- **Nur ein einstellbarer Regler da** (z. B. MP1584): geht auch. Ihn aber **vorher allein** mit dem Multimeter auf 3,3 V einstellen, das Poti mit einem Tropfen Lack sichern und einen 100-µF-Kondensator an OUT gegen GND setzen. Über 3,6 V nimmt der ESP32-S3 Schaden.

**Mit HX711** (deine Teile):

| Pin | Kabelfarbe | Signal | Waage (HX711) | NFC (PN532) | Scanner (GM861) |
|---|---|---|---|---|---|
| 1 | rot | **5 V** | über den 3,3-V-Regler | VCC | über den 3,3-V-Regler |
| 2 | schwarz | **GND** | GND | GND | GND |
| 3 | gelb | SDA (GPIO 10) | – | SDA | – |
| 4 | grün | SCL (GPIO 11) | – | SCL | – |
| 5 | blau | GPIO 12 | **SCK** | – | – |
| 6 | weiß | GPIO 13 | – | – | **TXD** |
| 7 | braun | GPIO 14 | **DT** (DOUT) | – (kein Reset-Draht) | – |
| 8 | (nicht am Kabel) | GPIO 21 | – | – | RXD (optional) |

```
WT32-SC01 Plus, I/O-Kabel
 rot    5V ──┬──────────── PN532 VCC
             └── [3,3-V-Regler IN]
                  3,3-V-Regler OUT ──┬── HX711 VCC
                                     └── GM861 VCC
 schwarz GND ─┬─────────── PN532 GND
              ├─────────── Regler GND
              ├─────────── HX711 GND
              └─────────── GM861 GND
 gelb   SDA ────────────── PN532 SDA
 grün   SCL ────────────── PN532 SCL
 blau   IO12 ───────────── HX711 SCK
 weiß   IO13 ───────────── GM861 TXD
 braun  IO14 ───────────── HX711 DT
```

**Mit NAU7802** statt HX711: Die NAU7802 hängt wie der PN532 an Pin 1 bis 4 (VIN an 5 V, GND, SDA, SCL), parallel zum PN532. Pin 5 und 7 bleiben frei, an Pin 7 darf dann wahlweise der Reset-Draht des PN532 (RSTPDN).

Drei Regeln, die Schäden vermeiden:

1. **Immer stromlos arbeiten.** USB abziehen, bevor du etwas an- oder absteckst.
2. **Kein 5-V-Signal an einen Datenpin.** Der ESP32-S3 verträgt an GPIO 10 bis 14 und 21 nur 3,3 V. Deshalb läuft der **HX711 an 3,3 V**: An 5 V würde sein DT-Ausgang 5 V an GPIO 14 legen.
3. **Den PN532 direkt an Pin 1 bis 4** anschließen, nicht über die STEMMA-QT-Buchse einer NAU7802 (die liefert nur 3,3 V, der PN532 braucht 5 V).

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

### Stufe 1: Waage mit HX711 und Wägezelle

1. **USB abziehen.**
2. **3,3-V-Regler anschließen** ([so geht's](#der-33-v-regler)), noch ohne etwas dahinter: **IN** an Pin 1 (rot, 5 V), **GND** an Pin 2 (schwarz). USB anstecken und am Ausgang messen (OUT gegen GND): **3,2 bis 3,4 V**? Wieder abziehen.
3. **Wägezelle an den HX711.** Die Adern sind meist so belegt, die Beschriftung der Platine zählt:

   | Ader der Wägezelle | HX711 |
   |---|---|
   | rot | E+ |
   | schwarz | E− |
   | weiß | A− |
   | grün | A+ |

   B+ und B− bleiben frei.
4. **HX711 an Regler und I/O-Kabel:**

   | HX711 | geht an |
   |---|---|
   | VCC | **OUT des 3,3-V-Reglers** (nicht an 5 V!) |
   | GND | Pin 2, schwarz |
   | SCK | Pin 5, blau (GPIO 12) |
   | DT (auch DOUT) | Pin 7, braun (GPIO 14) |

   Hat deine HX711-Platine zwei Versorgungspins (VCC und VDD), kommen **beide** an 3,3 V.
5. Alle Lötstellen einzeln mit Schrumpfschlauch isolieren.
6. **Die Wägezelle richtig montieren:** Ein Ende fest an den Boden, das andere trägt die Waagschale. Der Pfeil auf dem Balken zeigt nach unten (Richtung der Last). Die Mitte des Balkens (die weiße Vergussmasse) darf nirgends anliegen.
7. USB anstecken. Die Waage sucht beim Start zuerst eine NAU7802 und dann den HX711. Hat sie ihn gefunden, merkt sie sich das.

**Prüfen:**

- [ ] Oben steht nicht mehr „Waagen-ADC fehlt“. Jetzt darf **„NFC-Reader fehlt (PN532)“** dastehen, der kommt erst in Stufe 2.
- [ ] Die Waage zeigt eine Zahl. Mit der Hand leicht auf die Schale drücken lässt sie **steigen**.
- [ ] Kalibrieren: Einstellungen → Waage → Kalibrierung. Leer tarieren, ein bekanntes Gewicht (z. B. 1000 g, eine volle Wasserflasche auf der Küchenwaage abgewogen) auflegen, Wert eingeben, speichern.
- [ ] Danach stimmt das bekannte Gewicht auf ±2 g.

**Wenn es nicht geht:**

| Anzeige | Ursache und Abhilfe |
|---|---|
| „Waagen-ADC fehlt (NAU7802)“ gleich nach dem ersten Start | Die Waage hat den HX711 nicht gefunden. 3,3 V an VCC messen; SCK an Pin 5 und DT an Pin 7 prüfen (nicht vertauscht). Danach neu starten. |
| „Waagen-ADC fehlt (HX711)“ | Der HX711 war schon einmal da und antwortet nicht mehr: Wackelkontakt an VCC, GND, SCK oder DT. |
| „Wägezelle verpolt (A+/A−)“ oder Werte sinken beim Drücken | Weiß und grün an A+/A− tauschen, oder die Zelle umdrehen (Pfeil nach unten). |
| „Wägezelle unruhig“ | Eine Ader hat Wackelkontakt, oder die Zelle berührt das Gehäuse. Lötstellen prüfen, den Balken frei schweben lassen. |
| „Waage nicht kalibriert“ | Kalibrierung wie oben machen. |
| Beim Kalibrieren „Waage unruhig (Streuung X von Y)“ | Die Waage mittelt 1,5 Sekunden lang. Schwankt der Rohwert in dieser Zeit um mehr als ein Fünftel des Sprungs durch das Gewicht, rechnet sie nicht. Erst die Ursache beseitigen (siehe „Wenn das Gewicht wandert“), Gewicht ruhig auflegen, Hände weg, dann noch einmal. |
| Beim Kalibrieren „Unplausibel: X Zähler für Y g“ | Der Rohwert hat sich durch das Gewicht kaum geändert (unter 1 Zähler je Gramm) oder unglaublich stark. Sinkt oder steigt der Rohwert auf der Seite Waage beim Auflegen gar nicht, ist die Wägezelle nicht richtig angeschlossen. Vor dem Auflegen **Tara** drücken, Gewicht auflegen, erst dann den Wert eingeben und „Berechnen“. |

**Wenn das Gewicht wandert (Drift):**

**Zuerst kalibrieren.** Steht auf der Seite Waage beim Kalibrierfaktor **1.0000**, ist die Waage nicht kalibriert und zeigt Rohzähler als „Gramm“: 100 Zähler Rauschen sind dann „100 g“, und der Drift erscheint riesig. Mit dem Faktor kommen Gramm heraus (beim HX711 mit 3,3 V etwa 200 bis 400 Zähler je Gramm). Die Zeile „Drift“ zeigt bis dahin nur Zähler pro Minute.

Weboberfläche → **Waage**, Schale leer lassen und zuschauen. Die Seite zeichnet den Rohwert der letzten 20 Minuten als Kurve und zeigt in der Zeile **„Drift (letzte Minute)“** (erst nach etwa 30 Sekunden) in g pro Minute, wie schnell er läuft. Die Kurve sagt, woran es liegt:

| Die Kurve … | Ursache und Abhilfe |
|---|---|
| läuft in **eine Richtung und wird flacher** | Aufwärmen: Wägezelle und HX711 erwärmen sich in den ersten 10 bis 15 Minuten. Normal. Erst danach tarieren und kalibrieren. |
| läuft **dauernd weiter in eine Richtung** | Wärme von außen: Das Display wird warm. Sitzt die Wägezelle direkt daneben oder unter der Sonne, hat der Balken eine ungleiche Temperatur. Zelle thermisch vom Display trennen, Gehäuse nicht in Sonne oder Zugluft stellen. |
| macht **Sprünge** (Stufen) | Mechanisch: Die Zelle berührt das Gehäuse oder die Schale, ein Kabel zieht am Balken, eine Schraube sitzt locker. Die vier Adern der Zelle mit Heißkleber oder Klebeband entlasten. |
| ist **unruhig** und „Verworfene Messwerte“ steigen | Elektrisch: SCK-/DT-Draht kurz halten (unter 20 cm), VCC und GND des HX711 mit **100 µF und 100 nF** direkt an den Pins stützen, Lötstellen prüfen. |
| wandert nur, wenn die Waage **liest, scannt oder WLAN nutzt** | Die Versorgung bricht ein oder der PN532 stört: HX711 und Zellenkabel weit weg von der PN532-Antenne legen, Adern der Zelle verdrillen, die 100 µF am Regler-Ausgang nicht weglassen. |

Läuft sie nach 15 Minuten weiter als etwa ein Gramm pro Minute, ist es kein Aufwärmen mehr. Die Seite „Waage“ als Screenshot und die Zeilen `Scale:` aus dem Log schicken, dann lässt sich die Ursache eingrenzen. **„Verworfene Messwerte“** zählt Messungen, die die Waage aussortiert hat (Leitung hängt auf High oder Low, Eingang offen). Auf einer gesunden Waage bleibt die Zahl bei 0.

**Findet die Waage den Chip nicht, obwohl alles richtig angeschlossen ist?** Weboberfläche → **Waage** → Karte **Waagen-Chip**. Normalerweise bleibt dort „Automatisch erkennen“. Läuft ein HX711 nach dem Einschalten langsam an, die **Wartezeit** erhöhen (Standard 1500 ms, bis 5000 ms). Hilft das nicht, den Chip **fest vorgeben** („HX711 fest vorgeben“): Die Waage sucht dann nur noch diesen Chip, nimmt ihn auch ohne bisherigen Fund als vorhanden an und lässt die Pins 5 und 7 für ihn frei. Speichern und neu starten, die Einstellung gilt ab dem nächsten Start. Wieder auf „Automatisch“ stellen, sobald es läuft.

Ein Gerät ganz ohne Waage (nur NFC)? Dann in Einstellungen → Waage **„Waage vorhanden“** ausschalten, die Meldung verschwindet.

<details>
<summary>Stufe 1 mit NAU7802 statt HX711</summary>

1. **Wägezelle an die NAU7802:** rot E+, schwarz E−, weiß A+, grün A− (auch hier zählt die Beschriftung; bei „Wägezelle verpolt“ weiß und grün tauschen).
2. **NAU7802 an das I/O-Kabel:** VIN an Pin 1 (5 V), GND an Pin 2, SDA an Pin 3, SCL an Pin 4. Fertige STEMMA-QT-Kabel haben oft eine **andere Farbfolge** als das WT32-Kabel: nach der Beschriftung gehen, nicht nach der Farbe.
3. Prüfen wie oben. Bleibt „Kein Modul am I2C-Bus“: 5 V und GND an VIN/GND messen, SDA und SCL vertauscht?

</details>

### Stufe 2: NFC-Leser (PN532 V3)

1. USB abziehen.
2. **DIP-Schalter am PN532 auf I2C stellen: Schalter 1 = ON, Schalter 2 = OFF.** Steht er anders (HSU oder SPI), antwortet der Leser gar nicht. Die Stellungen sind auf der Platine aufgedruckt.
3. **Von der Rückseite löten**, vorne ist im Gehäuse kein Platz. Vier Drähte an die Stiftleiste mit GND, VCC, SDA, SCL:

   | PN532 | I/O-Kabel |
   |---|---|
   | VCC | Pin 1, rot (5 V) |
   | GND | Pin 2, schwarz |
   | SDA | Pin 3, gelb |
   | SCL | Pin 4, grün |

   **Kein Reset-Draht.** Pin 7 gehört beim HX711-Aufbau der Waage (DT). Die Firmware weiß das und kommt ohne Reset-Leitung aus.

   Am bequemsten: an den PN532 ein kurzes Kabel mit **JST-SH-1,0-Stecker** löten. Dann geht das Löten außerhalb des Gehäuses, und beim Zusammenbau wird nur eingesteckt. Die Belegung solcher Fremdkabel steht in [BUILDING.md](../BUILDING.md#pn532--cable-pinout-jst-sh-10mm-5-pin-3rd-party-colors).
4. USB anstecken.

**Prüfen:**

- [ ] Keine Meldung mehr oben.
- [ ] Einen NFC-Aufkleber oder eine Bambu-Spule auflegen: Nach 1 bis 2 Sekunden wird der Tag gelesen. Eine bekannte Spule erscheint mit Name und Restgewicht.
- [ ] Ein unbekannter Tag: Die Waage bietet **Verknüpfen** oder **Anlegen** an.

**NFC-Aufkleber (NTAG215, 25 mm) an die Spule:** Der Leser sitzt unter der Mitte der Waagschale und liest etwa 2 bis 4 cm weit. Den Aufkleber deshalb **auf die Unterseite der Spulenwange nahe der Nabe** kleben, so dass er beim Auflegen über der Mitte liegt. Nicht auf Metall (z. B. Alu-Folie, Metallspulen), das schirmt ab. Ein NTAG215 hat Platz für alle Formate, die die Waage schreibt.

**Wenn es nicht geht:**

| Anzeige | Ursache und Abhilfe |
|---|---|
| „NFC-Reader fehlt (PN532)“ | DIP-Schalter prüfen (1 = ON, 2 = OFF). VCC muss an **5 V** hängen. SDA und SCL nicht vertauscht? |
| „Kein Modul am I2C-Bus“ | Beim HX711-Aufbau hängt nur der PN532 am Bus: VCC/GND/SDA/SCL am PN532 prüfen. |
| Tag wird nicht gelesen | Den Tag mittig auflegen. Metall zwischen Tag und Leser schirmt ab. |
| Leser hängt nach Stunden („NFC-Reader stumm“) | Ohne Reset-Draht bringt nur ein Neustart ihn zurück. Abhilfe: den optionalen Reset-Draht unten nachrüsten. |

**Optional: Reset-Draht an Pin 8.** Damit holt die Waage einen hängenden Leser selbst zurück, ohne Neustart.

1. USB abziehen. Einen Draht vom Pad **RSTPDN** des PN532 an **Pin 8** der Buchse (GPIO 21) löten. Pin 8 ist nicht am 7-poligen Kabel: einzelne Crimp-Hülse oder direkt an den Kontakt.
2. Pin 8 ist sonst die optionale RXD-Leitung zum Scanner. Liegt dort schon ein Scanner-Draht, diesen entfernen. Zum Scannen braucht die Waage ihn nicht, nur zum Einstellen von Licht und LED.
3. USB anstecken. Am Gerät: Einstellungen → System → **NFC-Reset prüfen**. Meldet die Waage „funktioniert“, nutzt sie den Draht ab dem nächsten Start. Vorher treibt sie Pin 8 nie.

### Stufe 3: Ins Gehäuse

Erst wenn Stufe 1 und 2 funktionieren. Die Reihenfolge und Fotos stehen in [BUILDING.md](../BUILDING.md#assembly-steps). Worauf es ankommt:

1. **USB-C-Einbaukabel:** Den Stecker in die USB-C-Buchse des WT32-SC01 Plus, die Einbaubuchse von innen in die Öffnung der Gehäusewand schrauben. Das Kabel in einem weiten Bogen legen, nicht knicken. Über diese Buchse geht später auch das Flashen per USB.
2. Die Kabel so verlegen, dass sie **nicht an die Waagschale oder den Wägebalken drücken**. Sonst wiegt die Waage das Kabel mit, und das Gewicht schwankt.
3. Den 3,3-V-Regler und den HX711 mit doppelseitigem Klebeband an die Gehäusewand, nicht an den Balken.
4. Nach dem Zuschrauben die Kalibrierung aus Stufe 1 **noch einmal** machen: Das Gehäuse verändert den Nullpunkt.

### Stufe 4: Barcode-Scanner (optional)

Der Scanner liest Barcodes auf Filament-Kartons und QR-Codes auf Etiketten. Er ist in der Firmware **ab Werk ausgeschaltet**. Ohne ihn läuft alles andere normal.

> [!WARNING]
> Der GM861 läuft mit **3,3 V** (höchstens 70 mA, so steht es auch beim Edelstahl-Modell). Der I/O-Stecker hat aber nur **5 V**. Den Scanner **nicht direkt an Pin 1** hängen, sondern an den **3,3-V-Regler aus Stufe 1** (der versorgt HX711 und Scanner zusammen). Läuft der Scanner mit 3,3 V, sendet er auch mit 3,3 V und darf direkt an GPIO 13.

Der GM861 im Edelstahlgehäuse hat ein Kabel mit UART und USB. **Die Farben der Adern sagen nichts**, es zählt die Belegung auf dem Beipackzettel bzw. die Reihenfolge am Stecker (Pin 1 ist markiert). Beim 6-poligen SH-1,0-Stecker:

| GM861 Pin | Signal | geht an |
|---|---|---|
| 1 | VCC (3,3 V) | Ausgang (OUT/3V3) des 3,3-V-Reglers |
| 2 | TXD | **Pin 6 (weiß, GPIO 13)** |
| 3 | RXD | Pin 8 (GPIO 21), **optional** |
| 4 | GND | Pin 2 (schwarz) |
| 5 | D− (USB) | frei lassen |
| 6 | D+ (USB) | frei lassen |

VCC kommt an **OUT des 3,3-V-Reglers** aus Stufe 1. Mit NAU7802-Aufbau gibt es den Regler noch nicht: dann jetzt einen anschließen, **IN** an Pin 1 (rot, 5 V), **GND** an Pin 2 (schwarz).

RXD ist nur nötig, wenn die Waage dem Scanner etwas sagen soll: Licht und grüne LED aus der Weboberfläche einstellen. Pin 8 ist nicht am 7-poligen Kabel, dafür einen einzelnen Draht an den achten Kontakt der Buchse (z. B. mit einer einzelnen JST-Crimp-Hülse).

**Schritt für Schritt:**

1. USB abziehen.
2. Ohne Regler aus Stufe 1: Regler an Pin 1 und 2 anschließen, **noch ohne Scanner**, und am Ausgang (OUT gegen GND) messen: **3,2 bis 3,4 V**?
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

Steht filament_to_bambuddy in den Einstellungen, druckt die Waage das Etikett im Layout, das du am Handy angeordnet hast. Mehr dazu unter [Etikettendrucker TYPONOS PM240](2-bedienung.md#etikettendrucker-typonos-pm240).

## Firmware installieren

Die Firmware dieses Forks ist größer als 3 MB und braucht die 6-MB-Partitionstabelle. **Einmal per USB flashen**, danach kommen Updates über WLAN.

### Weg A: Web Flasher (empfohlen)

1. Am PC **Chrome oder Edge** öffnen (Firefox und Safari können kein WebUSB).
2. `https://areyouthereandi2.github.io/SpoolmanScale/` aufrufen. Voraussetzung: [GitHub Pages ist aktiviert](3-wartung.md#einmalig-github-pages-einschalten) und der [erste Release](3-wartung.md#einen-release-machen) ist gebaut.
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

Details stehen in [BUILDING.md](../BUILDING.md).

## Ersteinrichtung

1. **Sprache, Zeitzone, WLAN** am Bildschirm, falls der Web Flasher das WLAN nicht schon übertragen hat.
2. **Backend: BamBuddy.** URL eintragen, z. B. `http://zimaboard:8000`. Den API-Schlüssel in der **Weboberfläche** der Waage eintragen (die IP steht am Info-Bildschirm). Er braucht in BamBuddy → Settings → API Keys:
   - **Read Status** und **Manage Inventory** (Grundfunktionen),
   - **Control Printer** (Steckdosen),
   - **Manage Archives** (Bewertung nach dem Druck).

   Läuft BamBuddy mit Spoolman in Docker, unter **Backend → Spoolman-Adresse für die Waage** die LAN-Adresse von Spoolman eintragen (z. B. `http://192.168.1.5:7912`), siehe [unten](2-bedienung.md#bambuddy-mit-spoolman-welches-inventar).
3. **Kalibrieren:** Einstellungen → Waage → Kalibrierung. Leer tarieren, ein bekanntes Gewicht von etwa 1000 g auflegen, Wert eingeben, speichern.
4. **Steckdose in BamBuddy** dem Drucker zuordnen (Settings → Smart Plugs).
   - Ist dort „Auto Power Off“ an, schaltet BamBuddy selbst ab, und die Waage sagt das nur.
   - Sonst wartet die Waage, bis Düse und Bett abgekühlt sind (Standard 50 °C).
5. **BamBuddy-Optionen** an der Waage: „Nach dem Wiegen ins AMS“ und „Nach dem Druck fragen“ einschalten.
6. **filament_to_bambuddy-Adresse** (Weboberfläche → Einstellungen → Barcode-Scanner), z. B. `http://zimaboard:8088`. Wird nur vom Scanner gebraucht und kann auch später eingetragen werden.

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
