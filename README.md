# sun2000Tool

Kleines C++17-Programm zum Auslesen einer Huawei-SUN2000-Anlage per Modbus TCP.

## Voraussetzungen

- C++-Compiler (z. B. Xcode Command Line Tools)
- CMake 3.10 oder neuer
- `libmodbus` (macOS: `brew install libmodbus`)

## Bauen und starten

```bash
cmake -S . -B build
cmake --build build
./build/sun2000Tool --help
```

### Native macOS-Menueleisten-App

`sun2000Menu` ist eine eigenstaendige SwiftUI-/AppKit-App fuer macOS 15 und
neuer. Sie startet `sun2000Tool` nicht selbst, sondern liest in konfigurierbaren
Abstaenden die neueste passende Stunden-CSV-Datei ein.

```bash
cd sun2000Menu
swift test
./build-app.sh
open ../build/sun2000Menu.app
```

Beim ersten Start in den Einstellungen eine CSV-Basisdatei auswaehlen (z. B.
`sun2000.csv` oder eine zugehoerige Datei wie
`sun2000_2026-10-24T06.csv`). Die App erkennt daraus die stundenweise
Dateinamenreihe. Das Standardintervall fuer das erneute Einlesen betraegt
10 Sekunden; es steuert ausschliesslich den Dateizugriff und startet keine
Wechselrichterabfrage. `sun2000Tool` muss separat mit `--csv` laufen und in
dieselbe CSV-Dateireihe schreiben. Die Auswahl fuer die direkte
Menueleistenanzeige wird nach dem ersten erfolgreichen Einlesen aus den
verfuegbaren CSV-Signalen angeboten. Linksklick zeigt alle uebrigen Werte;
Rechtsklick oeffnet die Einstellungen. Die App wird als Menu-bar-only-App ohne
Dock-Symbol gestartet.

## Modbus-Endpunkt finden

Modbus TCP muss in der Wechselrichter-/Dongle-Konfiguration freigeschaltet sein.
Scanne nur ein eigenes, bekanntes LAN- oder WLAN-Subnetz, zum Beispiel:

```bash
./build/sun2000Tool scan --cidr 192.168.1.0/24
```

Der Scan prueft TCP-Port 502 und 6607 sowie Unit-IDs 0 und 1. Ein offener Port
ohne Modbus-Antwort wird nur als Kandidat ausgegeben. Der Scan schreibt keine
Register und untersucht kein Netzwerk ohne explizite CIDR-Angabe.

## Polling

Eine laienverstaendliche Erklaerung der CSV-Messwerte und Registergruppen steht
in [docs/solar-registerwerte-einfach-erklaert.md](docs/solar-registerwerte-einfach-erklaert.md).

Die Polling-Architektur trennt Modbus-Zugriff (`ModbusClient`), die beiden
Ausgabekanäle (`CsvWriter`, `ConsoleReporter`), die Zusammenstellung der
Messwerte (`Sun2000Poller`) und die benannten Registeradressen in
`register_map.hpp`. Dadurch koennen Ausgabe und Geraetekommunikation unabhaengig
erweitert und getestet werden.

```bash
./build/sun2000Tool poll --host 192.168.1.50 --port 502 --unit-id 1 \
	--interval-seconds 10
```

CSV-Ausgabe ist standardmaessig deaktiviert. Mit `--csv` wird sie eingeschaltet;
optional kann mit `--log-file DATEI` ein anderer Basisname samt Zielverzeichnis
angegeben werden. Fuer jede UTC-Stunde wird bei der ersten Messung eine Datei
nach dem Muster `sun2000_YYYY-MM-DDTHH.csv` angelegt; weitere Messungen dieser
Stunde werden angehaengt. `--log-file /pfad/anlage.csv` erzeugt entsprechend
`/pfad/anlage_YYYY-MM-DDTHH.csv`. Ohne `--csv` werden keine CSV-Dateien angelegt
oder veraendert:

```bash
./build/sun2000Tool poll --host 192.168.1.50 --interval-seconds 10 \
	--csv --log-file sun2000.csv
```

Die Konsole zeigt standardmaessig eine kompakte Zusammenfassung mit den
wichtigsten Identifikations-, PV-, Wechselrichter-/Netz- und Batteriewerten.
In dieser Zusammenfassung werden Protokoll-Sentinelwerte sowie Werte ausserhalb
grosszuegiger physikalischer Grenzen ausgeblendet; plausible Nullwerte bleiben
sichtbar. Die vollstaendige Ansicht zeigt die gelesenen Rohwerte ungefiltert.
Register-Lesefehler werden im `summary`-Modus ebenfalls unterdrueckt, im Modus
`all` jedoch auf `stderr` ausgegeben. Fehler beim Verbindungsaufbau bleiben
unabhaengig vom Ausgabemodus sichtbar.
Mit `--console-output all` werden alle gelesenen Werte auf der Konsole
ausgegeben. `--console-output summary` waehlt die kompakte Ansicht explizit.
Beide Modi lesen dieselben Register; falls CSV mit `--csv` aktiviert ist,
enthaelt die Datei unabhaengig von der Konsolenauswahl alle erfolgreich
gelesenen Werte.

Das lesende Profil umfasst Wechselrichter-Identitaet und Kenndaten,
PV-Eingaenge, Netz-/Leistungswerte, Betriebszustand, Alarme, Energiezaehler und
Batterieregister fuer bis zu zwei Speichereinheiten. Die Registerdefinitionen
folgen der Huawei-Solar-Modbus-Registerliste. Dies ist weiterhin keine
vollstaendige Abdeckung aller firmware- und komponentenabhaengigen Register.
Optimizerdaten verwenden einen separaten Datei-/Synchronisationsmechanismus;
schreibbare Konfigurationsregister sowie detaillierte Telemetrie einzelner
Batteriepacks werden nicht ausgelesen. SOH-Kalibrierstatusregister sind enthalten.
Der CSV-Ringspeicher behaelt die aktuelle UTC-Stunde und die 23 vorangegangenen
Stunden (maximal 24 Stunden-Dateien). Aeltere passende Dateien werden beim
ersten Schreiben in ein Stundenfenster geloescht; andere Dateien im Verzeichnis
bleiben unangetastet. Das gilt gleichermassen fuer `--once` und Dauerbetrieb.
`--once` liest einmal. `Ctrl-C` beendet den Dauerbetrieb.
Die Identifikationsdaten enthalten auch die numerische Modell-ID aus Register
30070 (CSV-Signal `model_id`).
Wenn der blockweise Modellzugriff auf Register 30000–30014 fehlschlaegt, versucht
der Poller diese Register einzeln zu lesen. Ein erfolgreicher Einzelzugriff wird
zum Vergleich unter `model_name_individual` ausgegeben.
Die CSV-Datei enthaelt ausschliesslich CSV-Datensaetze. Auf der Konsole werden
die Messwerte separat und nach Identifikation, PV-Eingaengen, Wechselrichter/
Netz und Batterie gruppiert ausgegeben; Fehler erscheinen auf `stderr`.
Epoch-Zeitfelder behalten ihren Sekunden-Rohwert und erhalten zusaetzlich ein
ISO-8601-UTC-Feld (`<signal>_utc`); nicht gesetzte Sentinel-Werte werden dort
als `N/A` ausgegeben.

Port 502 ist haeufig, Huawei-Geraete koennen aber auch Port 6607 verwenden.
Bei SDongle-Verbindungen ist Unit-ID 1 haeufig; beim direkten Wechselrichterzugriff
kann Unit-ID 0 gelten. Die tatsaechlichen Einstellungen haengen vom Zugangspfad
und der Firmware ab. Huawei-Geraete erlauben oft nur eine aktive Modbus-Verbindung;
andere Modbus-Clients sollten waehrend des Pollings getrennt sein.

Der Poller fuehrt ausschliesslich Modbus-Leseoperationen aus. Modbus TCP sollte
nicht aus dem oeffentlichen Internet erreichbar sein.
