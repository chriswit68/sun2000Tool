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

```bash
./build/sun2000Tool poll --host 192.168.1.50 --port 502 --unit-id 1 \
	--interval-seconds 10 --log-file sun2000.csv
```

Der erste lesende Profilumfang umfasst Wechselrichter-Identitaet, PV-Eingaenge,
Netz-/Leistungswerte und Batterieaggregate. Die Registerdefinitionen folgen der
Huawei-Solar-Modbus-Registerliste. Dies ist ein Startprofil, keine vollstaendige
Abdeckung aller firmware- und komponentenabhaengigen Register. Optimizer,
Batterie-Packdetails und schreibbare Konfigurationsregister sind nicht enthalten.
Die Datei wird im CSV-Format fortlaufend erweitert; Zeitstempel sind UTC.
`--once` liest einmal. `Ctrl-C` beendet den Dauerbetrieb.

Port 502 ist haeufig, Huawei-Geraete koennen aber auch Port 6607 verwenden.
Bei SDongle-Verbindungen ist Unit-ID 1 haeufig; beim direkten Wechselrichterzugriff
kann Unit-ID 0 gelten. Die tatsaechlichen Einstellungen haengen vom Zugangspfad
und der Firmware ab. Huawei-Geraete erlauben oft nur eine aktive Modbus-Verbindung;
andere Modbus-Clients sollten waehrend des Pollings getrennt sein.

Der Poller fuehrt ausschliesslich Modbus-Leseoperationen aus. Modbus TCP sollte
nicht aus dem oeffentlichen Internet erreichbar sein.
