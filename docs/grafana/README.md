# Grafana-Dashboard fuer sun2000Tool

Das Dashboard [sun2000-dashboard.json](sun2000-dashboard.json) visualisiert die
Messwerte aus der TimescaleDB-Tabelle `sun2000_telemetry`.

## Voraussetzungen und Import

1. In Grafana unter **Connections > Data sources** eine PostgreSQL-Datenquelle
   fuer dieselbe Datenbank einrichten, in die `sun2000Tool` schreibt:
   - Host: `wittis-ds423-01:5444`
   - Database: derselbe Datenbankname wie bei `sun2000Tool --db-name`
   - User: `pv_user`
   - TLS/SSL passend zur NAS-Konfiguration einstellen
   - TimescaleDB-Unterstuetzung in der Datenquelle aktivieren, falls die
     Grafana-Version diese Option anbietet
2. Das Dashboard-JSON unter **Dashboards > New > Import** importieren.
3. Beim Import die soeben angelegte PostgreSQL-Datenquelle auswaehlen.

Passwoerter gehoeren in Grafanas gesicherte Datenquellen-Konfiguration, nicht
in das Dashboard-JSON. Das Dashboard enthaelt keine Zugangsdaten.

## Inhalt

- **Kompaktwerte:** AC-Leistung, PV-Eingangsleistung, Batterie-Ladezustand,
  Batterieleistung, Tagesertrag und Wechselrichtertemperatur.
- **PV-Strings:** Spannung und Strom aller erfassten Strings.
- **Wechselrichter und Netz:** Phasen-/Leiterspannungen, Stroeme,
  Wirkleistung, Blindleistung, Frequenz und Wirkungsgrad.
- **Energie und Ertrag:** Energiezaehler des Wechselrichters, MPPT und
  Batteriesystems.
- **Batterie-Betriebswerte:** Ladezustand, Leistung, Spannung, Strom,
  Temperaturen, Status und Einheiten.
- **Status und Alarme:** Alarm-, Fehler-, Kommunikations- und Betriebsstatus.
- **Weitere Wechselrichter-Diagnosewerte:** Uebrige numerische Registerwerte.
- **Alle Signale:** Tabelle mit dem neuesten Wert je Signal im ausgewaehlten
  Zeitraum, einschliesslich Text- und Zahlenwert.
- **Identifikation und Versionen:** Neueste nichtnumerische Geraete-, Firmware-
  und Statuszeitwerte.

Die TimescaleDB-Archivierung schreibt beim UTC-Stundenwechsel den letzten
Messsatz der vorherigen Stunde. Daher erscheinen neue Zeitreihenwerte erst mit
dem naechsten Stundenwechsel, nicht bei jedem einzelnen Modbus-Poll. Das
Dashboard startet mit einem Zeitraum von sieben Tagen und aktualisiert die
Ansicht alle fuenf Minuten. Bei Abweichungen kann der Zeitraum oben rechts
angepasst werden.

## SQL-Datenmodell

Die Panels verwenden `timestamp` als Zeitachse und `numeric_value` fuer
numerische Messwerte. `value` bewahrt den Originaltext, `signal` identifiziert
das Register und `unit` enthaelt die Einheit. Textsignale werden in den
Tabellenpanels angezeigt und nicht als Zeitreihen geplottet.
