# Betrieb mit Docker / Synology Dockhand

Das Docker-Image baut `sun2000Tool` in einem Linux-Container und enthaelt nur
die Laufzeitbibliotheken. Der native macOS-Build bleibt davon unberuehrt: Auf
dem Mac kann weiterhin wie in der Haupt-README beschrieben mit CMake gebaut
und `./build/sun2000Tool` gestartet werden.

## Synology DS423 vorbereiten

1. Lege auf dem NAS ein Projektverzeichnis an, zum Beispiel
   `/volume1/docker/sun2000Tool`, und stelle dort den Projektinhalt bereit
   (etwa durch Klonen des Git-Repositories).
2. Erstelle darin das Verzeichnis `data`. Es wird als persistenter Speicher fuer
   die CSV-Stunden-Dateien eingebunden.
3. Ermittle die numerische UID und GID des DSM-Benutzers, der das Datenverzeichnis
   besitzen soll. Trage beide als `SUN2000_UID` und `SUN2000_GID` in Dockhand
   ein und stelle sicher, dass dieser Benutzer Schreibrechte auf `data` hat.
   Die numerischen Werte sind wichtig, weil Container und DSM unterschiedliche
   Benutzerdatenbanken haben koennen.
4. Lege in Dockhand einen Compose-Stack aus diesem Projekt an. Der Build-Kontext
   muss das Verzeichnis mit `Dockerfile` und `compose.yaml` sein. Das Image wird
   auf der DS423 fuer Linux `x86_64` gebaut.
5. Setze die Variablen aus `.env.example` in Dockhands Stack-Umgebung:
   - `SUN2000_HOST`: feste IP-Adresse des Wechselrichters
   - `TIMESCALE_DB_HOST` und `TIMESCALE_DB_PORT`: erreichbarer DB-Endpunkt
   - `PGPASSWORD`: Datenbankpasswort
   - `SUN2000_UID` und `SUN2000_GID`: Besitzerkennung fuer die CSV-Dateien
6. Starte den Stack und pruefe die Container-Logs auf erfolgreiche Messungen
   und den TimescaleDB-Schreibvorgang.

Wenn TimescaleDB auf demselben NAS laeuft, kann `TIMESCALE_DB_HOST` die NAS-IP
und `TIMESCALE_DB_PORT` den veroeffentlichten Port (zum Beispiel `5444`) nutzen.
Wenn beide Container einem gemeinsamen benutzerdefinierten Docker-Netzwerk
angehoeren, kann stattdessen der Datenbank-Containername und dessen interner
PostgreSQL-Port (haeufig `5432`) verwendet werden.

Der Container braucht normalerweise keine veroeffentlichten Ports und keinen
Host-Netzwerkmodus: Er verbindet sich ausgehend mit dem Wechselrichter und der
Datenbank. Stelle sicher, dass Docker-Netzwerk und NAS-Firewall diese
Verbindungen erlauben.

## Zugangsdaten

`PGPASSWORD` wird zur Laufzeit als Container-Umgebungsvariable verwendet.
Pflege es ausschliesslich in Dockhands geschuetzter Stack-Konfiguration und
niemals im Repository, in einem Image oder in einer eingecheckten Compose-Datei.
Umgebungsvariablen sind fuer privilegierte Docker-Administratoren einsehbar;
verwende, falls in deiner Dockhand-/Docker-Konfiguration verfuegbar, deren
Secret-Verwaltung.

Die lokale `.env`-Datei ist in `.dockerignore` ausgeschlossen und sollte nicht
ins Git-Repository aufgenommen werden. `.env.example` enthaelt ausschliesslich
Platzhalter.

## Daten und Neustart

`./data:/data` ist ein Bind-Mount: Stunden-CSV-Dateien und das Unterverzeichnis
`data/sun2000_snapshots` ueberleben Image-Updates und Container-Neustarts.
Snapshots werden pro Poll unter jeweils einem neuen Dateinamen atomar
veroeffentlicht, damit `sun2000Menu` ueber SMB nicht zeitgleich dieselbe Datei
liest, die der Container gerade aktualisiert. Snapshots aelter als 24 Stunden
werden bereinigt. `restart: unless-stopped` startet den Poller nach einem
NAS-/Docker-Neustart automatisch erneut, ausser der Container wurde absichtlich
gestoppt. Der eingebaute CSV-Ringspeicher behaelt maximal 24 Stunden-Dateien.

Das Compose-Setup aktiviert `--timescale-db`, daher ist auch `--csv` aktiv und
`PGPASSWORD` erforderlich. Die Datenbankarchivierung erfolgt weiterhin nur
beim UTC-Stundenwechsel; das Polling-Intervall steuert die Modbus-Abfrage.
