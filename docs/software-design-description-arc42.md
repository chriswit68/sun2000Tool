# Software Design Description – sun2000Tool und sun2000Menu

**Architekturbeschreibung nach arc42**  
**Stand:** 25. Oktober 2026  
**Dokumentstatus:** Beschreibung des implementierten Projektstands

## 1. Einführung und Ziele

### 1.1 Zweck des Systems

Das System besteht aus zwei getrennten Anwendungen:

- **sun2000Tool** liest Messwerte aus einem Huawei-SUN2000-Wechselrichter über
  Modbus TCP, gibt sie auf der Konsole aus und kann sie in stündliche CSV-Dateien
  schreiben. Optional archiviert es abgeschlossene Stunden in TimescaleDB.
- **sun2000Menu** liest diese Dateien auf macOS und zeigt ausgewählte Werte in
  der Menüleiste an. Es startet `sun2000Tool` nicht und fragt den Wechselrichter
  nicht selbst ab.

Die Anwendungen können auf unterschiedlichen Rechnern laufen. Der hier
vorgesehene Betrieb verwendet `sun2000Tool` als Container auf einer Synology
DS423 und `sun2000Menu` auf einem Mac. Die Dateien werden über einen gemeinsamen
SMB-Dateipfad zugänglich gemacht.

### 1.2 Qualitätsziele

| Priorität | Qualitätsziel | Bedeutung |
|---|---|---|
| 1 | Verlässliche Messwerterfassung | Einzelne Registerfehler sollen verfügbare Werte nicht verhindern; Fehler sollen erkennbar bleiben. |
| 2 | Konsistenter Dateizugriff | Leser sollen keine teilweise geschriebenen Poll-Datensätze erhalten. |
| 3 | Geringe Datenbanklast | Die Messung kann häufig pollen, während die Archivierung in Stundenintervallen erfolgt. |
| 4 | Begrenzter Dateibedarf | Stunden-CSV-Dateien und Poll-Snapshots haben jeweils eine begrenzte Aufbewahrungsdauer. |
| 5 | Getrennte Verantwortlichkeiten | Modbus-Poller, Dateiausgabe, Datenbankarchiv und Menüleistenanzeige sind unabhängig testbar. |
| 6 | Plattformübergreifender Betrieb | Der native macOS-Build bleibt möglich; zusätzlich kann das C++-Programm in einem Linux-Container laufen. |

### 1.3 Systemgrenzen

Dieses Dokument beschreibt die beiden Anwendungen und ihre Dateischnittstelle.
Nicht Bestandteil sind die interne Firmware des Wechselrichters, die DSM-/SMB-
Implementierung, TimescaleDB selbst und Grafana selbst.

## 2. Randbedingungen

### 2.1 Technische Randbedingungen

- `sun2000Tool` ist in C++17 geschrieben und wird mit CMake gebaut.
- Benötigte native Bibliotheken sind `libmodbus` und `libpq`.
- `libpq` wird direkt verwendet, damit C++17 beibehalten werden kann.
- `sun2000Menu` ist eine SwiftUI-/AppKit-Menüleisten-App für macOS 15 oder neuer.
- Der Datenzugriff von `sun2000Menu` erfolgt über das Dateisystem, nicht über
  eine Netzwerk-API.
- Der Synology-Container ist ein Debian-Linux-Image für `x86_64`; Persistenz wird
  über ein Bind-Mount für `/data` bereitgestellt.

### 2.2 Protokoll- und Betriebsrandbedingungen

- Modbus TCP verwendet standardmäßig Port 502 und Unit-ID 1; beides kann
  konfiguriert werden. Die tatsächlichen Einstellungen hängen vom Zugangspfad
  und der Wechselrichterkonfiguration ab.
- Der Poll-Zeitstempel ist UTC und wird einmal je Poll vor den Registerzugriffen
  erzeugt. Alle im Poll erfolgreich gelesenen Werte teilen diesen Zeitstempel.
- `--timescale-db` setzt `--csv` voraus und kann nicht mit `--once` verwendet
  werden.
- Das TimescaleDB-Passwort wird aus `PGPASSWORD` gelesen. Es ist nicht Teil der
  Kommandozeile oder des Quellcodes.
- Es wird eine einzige aktive Modbus-Verbindung pro laufendem Poller angenommen.
  Huawei-Geräte unterstützen häufig nur einen gleichzeitigen Modbus-Client.

### 2.3 Fachliche Grenzen

- Das Registerprofil ist ein lesendes Profil mit ausgewählten Registergruppen,
  keine vollständige Abdeckung sämtlicher Huawei-Register oder
  firmwareabhängiger Geräte.
- Die gelesenen Gruppen umfassen Geräteidentifikation, PV-Eingänge,
  Wechselrichter- und Netzwerte, Betriebszustände/Alarme, Energiezähler und
  ausgewählte Batterie- und Batteriepackwerte. Optimizerdaten und
  schreibbare Konfigurationsregister sind nicht Teil des Profils.
- Ein Poll kann unvollständig sein, wenn einzelne Register nicht gelesen werden
  können. Die lesbaren Werte werden weiterhin ausgegeben.
- `sun2000Menu` stellt nur den jeweils letzten Messsatz eines gelesenen CSV-
  Dokuments dar; es ist kein Diagramm- oder Historienbrowser.

## 3. Kontextabgrenzung

### 3.1 Fachlicher Kontext

```text
Huawei SUN2000
    │ Modbus TCP (lesende Registerabfragen)
    ▼
sun2000Tool ───────────────► Konsole / Container-Logs
    │
    ├── stündliche CSV-Dateien ─► Ringspeicher und TimescaleDB-Archivquelle
    │
    ├── unveränderliche Poll-Snapshots ─► SMB ─► sun2000Menu ─► macOS-Menüleiste
    │
    └── TimescaleDB (optionale Archivierung abgeschlossener Stunden)
                                      │
                                      └── Grafana liest Telemetrie aus der DB
```

### 3.2 Externe Schnittstellen

| Gegenstelle | Schnittstelle | Datenrichtung | Beschreibung |
|---|---|---|---|
| SUN2000 / Dongle | Modbus TCP, üblicherweise Port 502 oder 6607 | `sun2000Tool` liest | Registerwerte, Status und Identifikation |
| Lokales Dateisystem | CSV | `sun2000Tool` schreibt | Stunden-Ringspeicher und Snapshots |
| SMB-Dateifreigabe | Dateisystemzugriff | `sun2000Menu` liest | Zugriff vom Mac auf die im NAS-Datenverzeichnis gespeicherten CSV-Dateien |
| TimescaleDB/PostgreSQL | libpq / PostgreSQL-Protokoll | `sun2000Tool` schreibt | Schemaeinrichtung beim Start, anschließend Stundenarchivierung |
| Grafana | PostgreSQL-Datenquelle | Grafana liest | Visualisierung der archivierten Tabellenzeilen |
| macOS `UserDefaults` | lokale Anwendungseinstellungen | `sun2000Menu` liest/schreibt | CSV-Pfad, Lesetakt und Menüleisten-Auswahl |

## 4. Lösungsstrategie

1. **Polling und Darstellung trennen.** Der Wechselrichter wird ausschließlich
   von `sun2000Tool` gelesen. Die Menüleisten-App greift nur auf Dateien zu.
2. **Messungen nach UTC-Zeit organisieren.** Ein Poll erhält einen
   ISO-8601-UTC-Zeitstempel (`YYYY-MM-DDTHH:MM:SSZ`); Stunden-Dateinamen
   verwenden die UTC-Stunde.
3. **Zwei Dateiansichten mit unterschiedlichen Zwecken führen.**
   - Stunden-CSV: vollständige Poll-Historie der Stunde und Eingang für die
     stündliche TimescaleDB-Archivierung.
   - Poll-Snapshot: nur der vollständige Datensatz eines einzelnen Polls, unter
     einem pro Poll neuen Dateinamen für SMB-Leser.
4. **Dateien atomar veröffentlichen.** Das Programm schreibt zunächst eine
   temporäre Datei im selben Verzeichnis und benennt sie anschließend auf den
   Zielnamen um. Die Snapshots verwenden zusätzlich immer neue Namen; dadurch
   muss ein SMB-Leser nicht dieselbe Datei öffnen, die regelmäßig ersetzt wird.
5. **Datenbankzugriffe bündeln.** Beim Stundenwechsel wird die abgeschlossene
   CSV vollständig eingelesen und in einer Datenbanktransaktion archiviert.
   Die Datensätze werden innerhalb dieser Transaktion einzeln eingefügt oder
   aktualisiert.
6. **Menüleistenstatus aus dem neuesten Snapshot bilden.** Die App scannt das
   Snapshot-Verzeichnis, wählt den neuesten gültigen Snapshot und übernimmt
   daraus den neuesten vollständigen Messsatz.
7. **Fehler sichtbar machen.** Verbindungs-, Datei-, Parse- und
   Datenbankfehler werden als Konsolen-/Container-Log oder als Status in
   `sun2000Menu` sichtbar.

## 5. Bausteinsicht

### 5.1 `sun2000Tool`

| Baustein | Verantwortung |
|---|---|
| CLI / `main.cpp` | Modus und Optionen parsen, Konfiguration validieren, Komponenten verbinden, Pollschleife und Scanmodus ausführen |
| `ModbusClient` | Modbus-TCP-Verbindung, Holding-Register-Lesezugriffe und Stringdekodierung |
| `Sun2000Poller` | Registerprofil abarbeiten, Werte skalieren/formatieren, Poll-Zeitstempel vergeben und Ergebnisse an Ausgaben weiterleiten |
| Registerdefinitionen / Decoder | Registeradressen, Datentypen, Skalierungen und Dekodierung |
| `CsvWriter` | Stunden-Ringspeicher, atomare Veröffentlichung und Poll-Snapshots |
| `ConsoleReporter` | Gruppierte Konsolenausgabe und je nach Modus gefilterte oder vollständige Ausgabe |
| `TimescaleWriter` | CSV-Parsing, DB-Schema-/Hypertable-Einrichtung und transaktionale Archivierung |
| Netzwerk-Scanner | Begrenzte Prüfung explizit angegebener IPv4-CIDR-Bereiche auf typische Modbus-Ports und Unit-IDs |

### 5.2 CSV-Dateimodell und Abläufe

#### 5.2.1 Basisname und Verzeichnislayout

Für eine Basisdatei `/data/sun2000.csv` entstehen folgende Dateien:

```text
/data/
├── sun2000_YYYY-MM-DDTHH.csv       # eine Datei je UTC-Stunde
└── sun2000_snapshots/
    ├── YYYY-MM-DDTHH-MM-SSZ-<id>.csv
    └── ...
```

Der Basisname ist konfigurierbar (`--log-file`). Das Snapshot-Verzeichnis
verwendet den Dateistamm der Basisdatei plus `_snapshots`.

#### 5.2.2 Stunden-CSV

- Die Datei enthält den Header `timestamp_utc,signal,value,unit`.
- Jeder erfolgreiche Messwert eines Polls wird als eigene CSV-Zeile angefügt.
- Ein Datensatz enthält Poll-Zeitstempel, Signalnamen, unveränderten Textwert
  und Einheit. Felder werden CSV-konform in Anführungszeichen geschrieben;
  innere Anführungszeichen werden verdoppelt. NUL-Bytes werden beim Schreiben
  aus Textfeldern entfernt.
- Beim ersten Poll der UTC-Stunde wird die Stunden-Datei angelegt. Weitere
  Polls derselben Stunde werden zum vollständigen Inhalt hinzugefügt.
- Die bestehende Stunden-Datei wird für jeden Poll als Ganzes neu erzeugt:
  alter Dateiinhalt plus neue Zeilen werden in eine temporäre Datei geschrieben,
  geprüft und danach atomar auf den Stunden-Dateinamen umbenannt.
- Der Ringspeicher hält die aktuelle UTC-Stunde und die 23 vorhergehenden
  Stunden, maximal 24 passende Dateien. Andere Dateien im Datenverzeichnis
  werden nicht durch die Stundenbereinigung gelöscht.

Die vollständige Ersetzung ist Teil des aktuellen atomaren Publikationsmodells.
Für Clients über SMB dient der Snapshot-Pfad als bevorzugte Lesequelle, weil
die Stunden-Datei unter demselben Pfad immer wieder ersetzt wird.

#### 5.2.3 Poll-Snapshot

- Am Ende jedes vollständig abgearbeiteten Poll-Durchlaufs wird zusätzlich eine CSV mit
  Header und ausschließlich den Zeilen dieses Polls geschrieben.
- Der Dateiname enthält den UTC-Poll-Zeitstempel und einen eindeutigen Suffix.
  Ein Snapshot wird nach Veröffentlichung nicht mehr verändert.
- Auch der Snapshot wird zunächst unter einem temporären Namen geschrieben;
  erst nach vollständigem Schreiben wird er mit seinem endgültigen Namen
  veröffentlicht.
- Snapshots liegen in `<Basisname>_snapshots/`, getrennt von den
  Stunden-Dateien. Für die Beispielbasis heißt das Verzeichnis
  `sun2000_snapshots`.
- Der Writer entfernt Snapshots, deren im Namen codierter UTC-Zeitpunkt mehr
  als 24 Stunden zurückliegt.
- Bei einem effektiven Pollabstand von 60 Sekunden entstehen ungefähr 1.440
  Snapshots pro Tag; bei 10 Sekunden ungefähr 8.640. Das effektive Intervall
  umfasst die Poll-Laufzeit plus die konfigurierte Wartezeit. Der
  Alters-Retentionsmechanismus begrenzt die Zeit, nicht die exakte Dateianzahl.
- Der Snapshot erscheint am Ende eines Polls. Ein Poll kann aber wegen
  Verbindungs- oder Ausgabeproblemen fehlschlagen oder nur einen Teil der
  Registerwerte enthalten; ein veröffentlichter Snapshot bedeutet nicht
  automatisch, dass jedes Register erfolgreich gelesen wurde.

**Zweck:** SMB-Clients wie `sun2000Menu` lesen einen neu benannten, vollständigen
Poll, statt während des 60-Sekunden-Schreibzyklus immer wieder dieselbe
Stunden-Datei zu öffnen. Die Trennung reduziert das Risiko, dass Dateicache oder
offene SMB-Dateihandles einen Austausch der Stunden-Datei als unvollständigen
Inhalt sichtbar machen. Sie garantiert keine Fehlerfreiheit des SMB-Stacks,
verhindert aber die wiederholte Änderung des vom Menü gelesenen Snapshot-Pfads.

#### 5.2.4 Ablauf eines Polls

```text
run_poll
  └─ Sun2000Poller::poll_once
       ├─ Modbus-Verbindung aufbauen
       ├─ UTC-Zeitstempel einmal erzeugen
       ├─ Register nacheinander lesen und dekodieren
       ├─ pro lesbarem Messwert CsvWriter::write aufrufen
       │    └─ Zeile zunächst im Speicher puffern
       ├─ CsvWriter::flush
       │    ├─ Stunden-CSV mit allen bisher gespeicherten Stundenzeilen atomar ersetzen
       │    └─ neue Snapshot-CSV für genau diesen Poll atomar veröffentlichen
       └─ Pollstatistik/Fehlerstatus zurückgeben
```

#### 5.2.5 Stundenwechsel und TimescaleDB

Beim ersten Wert eines Polls, dessen UTC-Stunde von der aktiven Stunde abweicht:

1. Noch gepufferte Zeilen der vorherigen Stunde werden veröffentlicht.
2. Der `BeforeHourOpen`-Callback wird für die vorherige Stunden-CSV ausgeführt.
3. `TimescaleWriter` liest und parst alle Datensätze dieser Datei.
4. Alle Zeilen werden in einer PostgreSQL-Transaktion mit
   `(timestamp, signal)` als Konfliktschlüssel eingefügt oder aktualisiert.
5. Nur nach erfolgreichem Archivieren wird die neue Stunde aktiv.
6. Der aktuelle Poll wird am Ende in Stunden-CSV und Snapshot veröffentlicht.

Die Tabelle `sun2000_telemetry` enthält `timestamp`, `signal`, `value`,
`numeric_value` und `unit`. Nichtnumerische Textwerte behalten ihren Originaltext
und erhalten `NULL` in `numeric_value`.

Bei einem Pollintervall von 60 Sekunden enthält die abgeschlossene Stunde
typischerweise ungefähr 60 Messzeitpunkte je regelmäßig erfolgreichem Signal.
Registerfehler, Geräteausfall, Programmneustarts oder Pollzeiten über eine
Stundengrenze können die Anzahl verringern oder einzelne Zeitpunkte unvollständig
machen. Die Datenbank wird beim Stundenwechsel beschrieben, nicht bei jedem
Poll; das Einfügen erfolgt als Transaktion, derzeit mit einer SQL-Ausführung pro
Datensatz.

### 5.3 `sun2000Menu`

| Baustein | Verantwortung |
|---|---|
| `Sun2000MenuApp` / `MenuBarAppDelegate` | App-Lebenszyklus, Statusleisten-Item, Menü, Einstellungen-Fenster und Menüleistenanzeige |
| `AppSettings` | Persistente Einstellungen mit `UserDefaults`: CSV-Basispfad, Intervall und ausgewählte Signale |
| `PollingService` | Zeitgesteuertes Einlesen im Hintergrund, Status und aktuelle Werte |
| `TelemetryCSVFile` | Normalisierung des gewählten Basis-/Stundenpfads, Ermittlung des neuesten Snapshots bzw. Legacy-Stundenfiles |
| `TelemetryCSVParser` | CSV-Parsing, Validierung des Headers, Auswahl des neuesten Messsatzes und Überspringen von NUL-haltigen physischen Zeilen |
| `TelemetrySignalCatalog` | Anzeigenamen, Gruppen, Tooltips und kompakte Menubar-Labels/-Werte |
| `SettingsView` | CSV-Pfad, Leseintervall und gruppierte Registerauswahl |

### 5.4 Zuständigkeit für gespeicherte Informationen

| Daten | Autorität | Aufbewahrung |
|---|---|---|
| Aktuelle Gerätedaten | neuester Poll-Snapshot | maximal 24 Stunden auf dem Dateisystem |
| Historie innerhalb einer Stunde | Stunden-CSV | Teil des 24-Dateien-Ringspeichers |
| Langzeittelemetrie | TimescaleDB | DB-seitige Aufbewahrung |
| Menüauswahl und Leseintervall | `UserDefaults` | bis zur Änderung durch den Benutzer |

## 6. Laufzeitsicht

### 6.1 Dauerbetrieb ohne Datenbank

1. Benutzer startet `sun2000Tool poll` mit Host und Intervall.
2. Optional wird `--csv` aktiviert.
3. Der Poller baut eine Modbus-Verbindung auf und liest das Registerprofil.
4. Konsole und gegebenenfalls CSV erhalten erfolgreich gelesene Werte.
5. Nach Abschluss des Polls werden Stunden-Datei und Snapshot veröffentlicht.
6. Die Anwendung wartet bis zum nächsten Pollintervall oder bis zum Signal zum
   Beenden. Die Wartezeit beginnt nach Ende des Polls; die Poll-Laufzeit kommt
   zum konfigurierten Intervall hinzu.

### 6.2 Dauerbetrieb mit TimescaleDB

Der Start erfordert `--timescale-db --csv` sowie ein gesetztes `PGPASSWORD`.
Beim Start wird die Datenbankverbindung eingerichtet und Tabelle/Hypertable
werden geprüft bzw. angelegt. Beim UTC-Stundenwechsel archiviert der Callback
den vollständigen Inhalt der gerade abgeschlossenen Stunden-Datei. Ein
Archivierungsfehler verhindert, dass der Writer die nächste Stunde als aktiv
übernimmt; der nächste Poll kann erneut versuchen, die vorherige Datei zu
archivieren.

### 6.3 Lesen in `sun2000Menu`

1. Der Timer löst sofort beim Start und danach im konfigurierten Intervall einen
   Lesevorgang aus.
2. Der Datei-Scan läuft in einem abgetrennten Hintergrundtask, damit die UI nicht
   blockiert.
3. Existiert ein Snapshot-Verzeichnis, wird der neueste gültig benannte Snapshot
   bevorzugt. Ist das Verzeichnis vorhanden, aber enthält keinen Snapshot,
   meldet die App einen Fehler, statt still auf die veränderliche Stunden-CSV
   zurückzufallen.
4. Nur wenn noch kein Snapshot-Verzeichnis existiert, wird als
   Rückwärtskompatibilität die neueste Stunden-CSV gelesen.
5. NUL-haltige physische Zeilen werden ausgelassen und im Status gezählt.
   Sonstige ungültige CSV-Strukturen führen zu einem Fehlerstatus.
6. Aus der Datei werden die Werte mit dem lexikographisch neuesten
   ISO-UTC-Zeitstempel als aktueller Satz übernommen.
7. Ausgewählte Register werden in kompakter Form im Statusleisten-Item
   dargestellt; übrige Werte sind im Menü erreichbar.

### 6.4 Start und Herunterfahren

- `--once` führt einen einzelnen Poll aus und liefert bei fatalem Fehler oder
  null erfolgreichen Modbus-Lesezugriffen einen Fehlerstatus.
- `Ctrl-C` beendet den Dauerbetrieb über ein Signalflag.
- In Docker wird der Container mit `restart: unless-stopped` betrieben.
- `sun2000Menu` stoppt seine Polling-Task beim Beenden der App.

## 7. Verteilungssicht

### 7.1 Synology DS423 / Docker

- `Dockerfile` baut das Linux-Binary in einem Debian-Build-Stage mit CMake,
  Compiler, `libmodbus` und `libpq`.
- Das Runtime-Image enthält das Binary und die erforderlichen Laufzeitbibliotheken.
- `compose.yaml` startet den Poller als unprivilegierten Prozess mit
  konfigurierbarer UID/GID.
- Der Hostordner wird nach `/data` gebunden. In diesem Verzeichnis liegen
  Stunden-CSV-Dateien und `sun2000_snapshots/`.
- Der Container benötigt ausgehende Verbindungen zum Wechselrichter und zur
  Datenbank; typischerweise muss kein Containerport veröffentlicht werden.
- Datenbankzugangsdaten werden zur Laufzeit bereitgestellt. Umgebungsvariablen
  können für privilegierte Docker-Administratoren sichtbar sein.

### 7.2 Mac

- `sun2000Menu` wird nativ mit Swift Package Manager gebaut und als
  menu-bar-only-App gestartet.
- Der konfigurierte CSV-Pfad zeigt auf die SMB-gemountete Basisdatei bzw. deren
  Verzeichnis. Die App leitet daraus `<stem>_snapshots` ab.
- Die macOS-App und der Synology-Container haben getrennte Releasezyklen.
  Änderungen am Snapshotformat oder Dateinamen müssen daher mit beiden
  Komponenten kompatibel ausgerollt werden.

### 7.3 Grafana

Grafana verbindet sich mit der PostgreSQL-/TimescaleDB-Datenquelle und liest die
Tabelle `sun2000_telemetry`. Das Dashboard verwendet `timestamp` als Zeitachse
und `numeric_value` für Zeitreihen; Textsignale werden in Tabellen angezeigt.

## 8. Querschnittliche Konzepte

### 8.1 Zeit

- Interne Datei- und Datenbankzeitstempel sind UTC.
- Ein Poll erzeugt einen Zeitstempel am Anfang des Poll-Durchlaufs; er ist nicht
  notwendigerweise der genaue Zeitpunkt jedes einzelnen Registerzugriffs.
- Stunden-Dateien richten sich nach der im Zeitstempel enthaltenen UTC-Stunde.
- Grafana zeigt den ursprünglichen CSV-Messzeitpunkt, nicht die spätere
  Archivierungszeit.

### 8.2 CSV-Schema und Parsing

```csv
timestamp_utc,signal,value,unit
2026-10-25T12:00:00Z,"active_power","520.000000","W"
```

Die vier Felder sind Zeitstempel, Signal-ID, Wert als Text und Einheit.
Numerische DB-Werte werden zusätzlich als `double precision` persistiert.
Anführungszeichen und Kommas in Textfeldern werden gemäß CSV-Regeln escaped.

### 8.3 Fehlerbehandlung

- Modbus-Verbindungsfehler sind fatal für den Poll. Einzelfehler beim Lesen
  werden gezählt; der Poll fährt mit übrigen Registern fort.
- Datei- und atomare Veröffentlichungsfehler werden nicht als Erfolg behandelt.
- Datenbanktransaktionen werden bei Insert-/Commit-Fehlern zurückgerollt.
- `sun2000Menu` unterscheidet noch nicht verfügbare Daten, Pollstatus und
  Datei-/Parsefehler. Beschädigte Zeilen mit NUL-Zeichen werden übersprungen und
  gemeldet; das ist eine Toleranzmaßnahme und kein Ersatz für eine fehlerfreie
  Quelle.

### 8.4 Parallelität und Dateizugriff

- Innerhalb eines `sun2000Tool`-Prozesses laufen Polls sequenziell.
- `sun2000Menu` darf die Datei unabhängig abfragen, weil es ausschließlich
  lesend arbeitet.
- Snapshots sind pro Poll neu benannte Dateien. Während der Veröffentlichung
  existiert der endgültige Name erst nach vollständigem Schreiben.
- Mehrere gleichzeitige `sun2000Tool`-Prozesse mit demselben Basisdateipfad sind
  nicht als unterstützter Betriebsmodus spezifiziert; sie könnten dieselben
  Stunden-CSV-Inhalte konkurrierend ersetzen.

## 9. Architekturentscheidungen

### 9.1 CSV-Datei statt direkter Kommunikation mit dem Menüleistenprozess

`sun2000Menu` startet den Poller nicht. Dadurch bleiben Wechselrichterzugriff,
Messintervall und Berechtigungen unabhängig von der macOS-UI. Die Datei ist die
lose gekoppelte Übergabeschnittstelle.

### 9.2 Snapshot neben der Stunden-CSV

Die Stunden-CSV muss fortlaufend ergänzt bzw. atomar ersetzt werden und wird für
Ringspeicher sowie Stundenarchivierung benötigt. Ein Snapshot pro Poll erhält
zusätzlich einen stabilen Dateinamen, damit SMB-Leser nicht dieselbe Datei
während eines Updates lesen. Die zusätzliche Dateimenge wird durch eine
24-Stunden-Bereinigung begrenzt.

### 9.3 Atomare Datei-Veröffentlichung

Temporärdatei und endgültige Datei liegen im selben Verzeichnis; nach
erfolgreichem Schreiben wird umbenannt. Das vermeidet partiell geschriebene
Inhalte auf der lokalen Dateisystemebene. SMB-Caches können eigene Semantik
haben, weshalb Snapshots zusätzlich Dateinamen-Unveränderlichkeit bieten.

### 9.4 Stundenweise Datenbanktransaktion

Die CSV sammelt Minutenpolls. Am Stundenwechsel werden alle Zeilen der
abgeschlossenen Datei in einer Transaktion in TimescaleDB übertragen. Damit
bleibt die Frequenz der DB-Transaktionen gering, während der Minutenverlauf
erhalten bleibt. Der Primärschlüssel `(timestamp, signal)` erlaubt idempotente
Wiederholung derselben Datei.

### 9.5 Native macOS-App plus Linux-Container

Die C++-Anwendung bleibt plattformneutral genug für CMake-Builds auf macOS und
für den separaten Linux-Container. Die SwiftUI-App wird weiterhin ausschließlich
für macOS gebaut.

## 10. Qualitätsanforderungen und Szenarien

| Szenario | Erwartetes Verhalten |
|---|---|
| SMB-Leser liest während eines Polls | Der neueste Snapshot wird erst sichtbar, wenn er vollständig geschrieben und umbenannt wurde. |
| Ein Modbus-Register antwortet nicht | Andere lesbare Register werden weiterhin ausgegeben; Fehlerzähler bzw. `all`-Diagnose machen das Teilergebnis nachvollziehbar. |
| Prozess läuft über UTC-Stundengrenze | Die vorherige CSV wird vor Aktivierung der neuen Stunde archiviert; anschließend beginnt der neue Stundenzyklus. |
| Datenbanktransaktion schlägt fehl | Transaktion wird zurückgerollt; der Stundenwechsel wird nicht als erfolgreich archiviert behandelt und kann erneut versucht werden. |
| NAS-/Container-Neustart | Persistente CSV-Daten bleiben im Bind-Mount; Docker startet den Dienst entsprechend der Restart-Regel erneut. |
| Snapshot fehlt | Bei vorhandenen Snapshot-Verzeichnis meldet `sun2000Menu` den fehlenden Snapshot; die mutable Stunden-CSV wird nicht als stiller Ersatz verwendet. |
| Alte Dateien sammeln sich an | Der Stunden-Ringspeicher behält höchstens 24 Stunden-Dateien; Snapshotdateien älter als 24 Stunden werden bereinigt. |

## 11. Risiken und offene Punkte

- **SMB-Kohärenz:** Snapshotdateien reduzieren das Risiko durch Dateiaustausch,
  ersetzen aber keinen End-to-End-Test mit dem konkreten DSM-, SMB- und
  macOS-Client.
- **Abhängigkeit von konsistentem Basisnamen:** `sun2000Menu` muss dieselbe
  Basisdatei verwenden, aus der `sun2000Tool` den Snapshot-Verzeichnisnamen
  bildet.
- **Keine Mehrinstanzensperre:** Ein zweiter Writer mit demselben Datenpfad ist
  nicht koordiniert.
- **Kein Snapshot pro fehlgeschlagenem Poll:** Polls mit fatalem Fehler können
  keinen oder nur einen unvollständigen Satz bereitstellen.
- **Snapshot-Aufkommen:** Eine kurze Poll-Wartezeit erzeugt viele einzelne
  Dateien. Die Altersbereinigung begrenzt die Retention auf 24 Stunden, aber
  die Zahl der Dateien richtet sich nach Pollrate und Polllaufzeit.
- **DB-Aufholstrategie:** Die Stunden-CSV wird bei Archivfehlern als Quelle
  erhalten. Es gibt keinen unabhängigen dauerhaften Archivierungsmarker; ein
  späterer Stundenstart versucht die direkt vorherige Stunde erneut. Längere
  Ausfälle oder übersprungene Stunden erfordern operative Kontrolle.
- **DB-Schreibmenge:** Die Transaktion umfasst pro Stunde ungefähr
  Pollanzahl × erfolgreich gelesene Signale. Bei langen CSV-Dateien kann das
  Speicherbedarf im Prozess und Transaktionsdauer erhöhen.
- **Text und Zahlen:** Die Datenbank bewahrt Originalwerte als Text. Grafana-
  Zeitreihen verwenden nur `numeric_value`; nichtnumerische Werte gehören in
  Tabellenpanels.
- **Tests:** Unit- und Integrationstests prüfen lokale Parser-, Writer- und
  Dateiverhaltensweisen. Sie simulieren nicht den SMB-Cache der konkreten
  Synology-/Mac-Installation und führen keinen Live-DB-Test aus.

## 12. Glossar

| Begriff | Bedeutung |
|---|---|
| Poll | Ein vollständiger Durchlauf, in dem `sun2000Tool` das Registerprofil abfragt. |
| Messsatz | Alle beim Poll erfolgreich gelesenen Signalwerte mit demselben UTC-Zeitstempel. |
| Stunden-CSV | UTC-Datei, die alle erfolgreichen Messzeilen einer Stunde sammelt. |
| Poll-Snapshot | Unveränderliche CSV-Datei mit den Daten eines einzelnen Polls. |
| Basisdatei | Konfigurierter Name/Pfad, etwa `/data/sun2000.csv`, aus dem Stunden- und Snapshotpfade abgeleitet werden. |
| Ringspeicher | Begrenzte Aufbewahrung der aktuellen und vorherigen 23 Stunden-CSV-Dateien. |
| Atomare Veröffentlichung | Temporärdatei vollständig schreiben und danach auf den endgültigen Dateinamen umbenennen. |
| SMB | Netzwerk-Dateifreigabeprotokoll, über das der Mac auf die NAS-Dateien zugreift. |
| Hypertable | TimescaleDB-Tabelle, deren Zeitspalte für Zeitreihendaten partitioniert wird. |
