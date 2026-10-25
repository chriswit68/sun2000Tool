# sun2000Tool

Kleines C++17-Programm zum Auslesen einer Huawei-SUN2000-Anlage per Modbus TCP.

## Voraussetzungen

- C++-Compiler (z. B. Xcode Command Line Tools)
- CMake 3.10 oder neuer
- `libmodbus` (macOS: `brew install libmodbus`)
- `libpq` fuer den optionalen TimescaleDB-Schreiber (macOS: `brew install libpq`)
- Docker fuer den optionalen Linux-Containerbetrieb (z. B. Synology Container
  Manager oder Dockhand)

Die TimescaleDB-Anbindung nutzt libpq direkt, damit der bestehende C++17-
Sprachstandard erhalten bleibt; libpqxx 8 setzt C++20 voraus.

## Bauen und starten

```bash
cmake -S . -B build
cmake --build build
./build/sun2000Tool --help
```

### Docker / Synology

Ein Linux-Container-Image mit persistentem CSV-Verzeichnis und
TimescaleDB-Anbindung ist fuer Synology und Dockhand vorbereitet. Einrichtung,
Netzwerk- und Berechtigungshinweise stehen in
[docs/docker/README.md](docs/docker/README.md). Der native macOS-Build bleibt
unveraendert und kann weiterhin direkt mit CMake gestartet werden.

Die Architektur beider Anwendungen einschliesslich CSV-Stundenarchiv,
Poll-Snapshots, SMB-Leseweg und TimescaleDB-Ablauf ist in der
[Software Design Description nach arc42](docs/software-design-description-arc42.md)
beschrieben.

### Native macOS-Menueleisten-App

`sun2000Menu` ist eine eigenstaendige SwiftUI-/AppKit-App fuer macOS 15 und
neuer. Sie startet `sun2000Tool` nicht selbst, sondern liest in konfigurierbaren
Abstaenden den neuesten Poll-Snapshot ein. Nur wenn noch kein Snapshot-
Verzeichnis vorhanden ist, verwendet sie als Legacy-Fallback die neueste
Stunden-CSV-Datei.

```bash
cd sun2000Menu
swift test
./build-app.sh
open ../build/sun2000Menu.app
```

Beim ersten Start in den Einstellungen die CSV-Basisdatei auswaehlen (z. B.
`sun2000.csv` oder eine zugehoerige Stunden-Datei wie
`sun2000_2026-10-24T06.csv`). Daraus leitet die App das Snapshot-Verzeichnis
`sun2000_snapshots` ab. Das Standardintervall fuer das erneute Einlesen betraegt
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
oder veraendert. Die Messzeilen eines Polls werden zuerst in eine temporaere
Datei geschrieben und danach atomar veroeffentlicht. Leser ueber ein Netzlaufwerk
sehen damit entweder den vorherigen vollstaendigen Stand oder den neuen
vollstaendigen Poll, aber keinen waehrend des Schreibens partiellen Datensatz:
Zusaetzlich wird jeder vollstaendige Poll unter einem neuen, unveraenderlichen
Dateinamen im Unterverzeichnis `sun2000_snapshots/` veroeffentlicht. Das ist fuer
`sun2000Menu` auf einem SMB-Netzlaufwerk gedacht: Die App findet bei jedem
Abfrageintervall den neuesten Snapshot, statt dieselbe Stunden-Datei zu lesen,
die der Container fortlaufend ersetzt. Snapshots aelter als 24 Stunden werden
entfernt; der bestehende Stunden-Ringspeicher bleibt unveraendert.

```bash
./build/sun2000Tool poll --host 192.168.1.50 --interval-seconds 10 \
	--csv --log-file sun2000.csv
```

### TimescaleDB-Archivierung

Optional kann `--timescale-db` zusammen mit `--csv` aktiviert werden. Beim
Wechsel in eine neue UTC-Stunde speichert das Programm vor dem Anlegen der neuen
CSV-Datei den vollstaendigen Inhalt der vorherigen Stunden-Datei in einem
Datenbankvorgang in TimescaleDB. Damit werden bei einem 60-Sekunden-Polling
typischerweise etwa 60 Messsaetze je Signal und Stunde archiviert, obwohl der
Datenbankzugriff nur einmal pro Stunde erfolgt. Beim Start eines Dauerlaufs wird
ebenfalls die vorhandene Datei der unmittelbar vorherigen Stunde archiviert.
`--once` schreibt nicht in die Datenbank; `--timescale-db` kann deshalb nicht
zusammen mit `--once` verwendet werden.

Fuer die TimescaleDB-Tabelle wird beim Start eine Retention-Policy eingerichtet.
`--db-retention-days TAGE` steuert die Aufbewahrung; Standard sind 356 Tage.
Zulaessig sind 1 bis 36500 Tage. Wird der Wert bei einem spaeteren Start
geaendert, ersetzt das Programm die bestehende Policy durch die neue Einstellung.
TimescaleDB entfernt abgelaufene Zeitbereiche im Hintergrund und kann nur ganze
Chunks loeschen; die tatsaechliche Aufbewahrung kann daher etwas ueber dem
eingestellten Grenzwert liegen. Die Policy erfordert, dass der DB-Benutzer
Eigentuemer der Hypertable ist.

Die Tabelle `sun2000_telemetry` und die Timescale-Hypertable werden bei der
Verbindung automatisch angelegt. Jede Zeile enthaelt Zeitstempel, Signalname,
Wert als Originaltext, fuer numerische Werte zusaetzlich `numeric_value`
(double precision), sowie Einheit. So bleiben Textsignale erhalten und
Messwerte koennen ohne Cast direkt numerisch ausgewertet werden. Die komplette
Stundendatei wird transaktional gespeichert; Wiederholungen werden ueber
`(timestamp, signal)` idempotent aktualisiert. Die genaue Messwertdarstellung
bleibt so auch fuer Textsignale erhalten. Die Retention-Policy entfernt
abgelaufene TimescaleDB-Chunks; sie wirkt unabhaengig von der 24-Stunden-
Dateibereinigung auf das Tabellen- und Indexvolumen.

Standardverbindung: Host `wittis-ds423-01`, Port `5444`, Datenbank `pv_user_db`,
Benutzer `pv_user`. Abweichungen koennen mit `--db-host`, `--db-port`,
`--db-name` und `--db-user` angegeben werden. Das Passwort wird ausschliesslich
ueber die Umgebungsvariable `PGPASSWORD` bezogen und darf nicht in Argumenten
oder im Quellcode abgelegt werden. Fuer einen Dauerlauf mit Datenbankarchiv:

```bash
./build/sun2000Tool poll --host 192.168.1.50 --interval-seconds 10 \
	--csv --log-file /pfad/sun2000.csv --timescale-db
```

Die Variable `PGPASSWORD` muss vor dem Start sicher in der Laufzeitumgebung
gesetzt sein. Kann die Datenbankverbindung oder das Schema beim Programmstart
nicht eingerichtet werden, bricht der Start mit einer Fehlermeldung ab. Scheitert
spaeter beim Stundenwechsel das CSV-Einlesen oder die Datenbanktransaktion, wird
die neue Stunden-Datei nicht vorzeitig geoeffnet; der Fehler wird ausgegeben und
beim naechsten Poll erneut versucht.

Ein importierbares Grafana-Dashboard mit kompakten Kennzahlen, Zeitreihen und
Tabellen fuer alle Signale ist in
[docs/grafana/README.md](docs/grafana/README.md) beschrieben.

Die Konsole zeigt standardmaessig eine kompakte Zusammenfassung mit den
wichtigsten Identifikations-, PV-, Wechselrichter-/Netz- und Batteriewerten.
In dieser Zusammenfassung werden Protokoll-Sentinelwerte sowie Werte ausserhalb
grosszuegiger physikalischer Grenzen ausgeblendet; plausible Nullwerte bleiben
sichtbar. Die vollstaendige Ansicht zeigt die gelesenen Rohwerte ungefiltert.
Register-Lesefehler werden im `summary`-Modus ebenfalls unterdrueckt, im Modus
`all` jedoch auf `stderr` ausgegeben. Fehler beim Verbindungsaufbau bleiben
unabhaengig vom Ausgabemodus sichtbar.
Im `--once`-Modus liefert das Programm einen Fehlerstatus, wenn die Verbindung
scheitert, ein fataler Ausgabe-/Pollfehler auftritt oder kein Modbus-Lesezugriff
erfolgreich war. Einzelne fehlgeschlagene Register werden als Teilergebnis
gezaehlt; lesbare Werte werden trotzdem ausgegeben und der Fehlergrund bleibt
im `all`-Modus sichtbar.
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
