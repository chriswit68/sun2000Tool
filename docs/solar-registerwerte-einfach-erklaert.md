# SUN2000-Register einfach erklaert

Der Wechselrichter stellt seinen Zustand und seine Messwerte in sogenannten
**Modbus-Registern** bereit. Ein Register ist ein nummerierter Speicherplatz im
Geraet. `sun2000Tool` liest diese Speicherplaetze und gibt die Werte mit einem
verstaendlichen CSV-Namen aus. Die verwendeten Registeradressen stehen in
[`src/register_map.hpp`](../src/register_map.hpp); welche Felder gelesen werden,
legt der Poller in [`src/sun2000_poller.cpp`](../src/sun2000_poller.cpp) fest.

Die folgende Uebersicht erklaert die wichtigsten ausgegebenen Werte und
Signalgruppen in Alltagssprache. Welche davon verfuegbar sind, haengt vom
Wechselrichtermodell, der Firmware und angeschlossenen Geraeten ab.

## Identifikation

| CSV-Signal | Bedeutung |
|---|---|
| `model_name`, `model_name_individual` | Modellbezeichnung des Wechselrichters. Die zweite Variante stammt aus einem alternativen Einzelzugriff. |
| `model_id` | Numerische Kennung des Modells. Sie ist kein lesbarer Modellname. |
| `serial_number` | Eindeutige Seriennummer des Wechselrichters. |
| `product_code`, `product_sales_area` | Produktkennung und vorgesehene Verkaufsregion. |
| `firmware_version`, `software_version`, `hardware_version` | Versionsstaende der Geraetesoftware und Hardware. |
| `monitoring_board_*`, `main_dsp_version`, `cpld_version`, `afci_version` | Seriennummern oder Softwarestaende einzelner interner Baugruppen. |
| `battery_unit_*_serial_number`, `battery_unit_*_*version` | Kennungen und Softwarestaende der Batterieeinheiten. |

## PV-Eingaenge

`pv_N_voltage` und `pv_N_current` beschreiben die einzelnen Gleichstrom-
Eingaenge (Strings) der Solarmodule. `N` ist die Nummer des Eingangs. Spannung
in Volt zeigt den elektrischen Druck, Strom in Ampere die momentan fliessende
Strommenge. Ein Eingang mit 0 A kann angeschlossen, aber gerade ohne messbaren
Strom sein.

`input_power` ist die vom Wechselrichter gemeldete gesamte PV-Eingangsleistung
in Watt. `mppt_count` gibt die Anzahl der unabhaengigen Arbeitspunkte-Regler
(MPPT) an. `mppt_N_dc_total_energy` ist die ueber den jeweiligen MPPT
aufgenommene Gleichstromenergie seit dessen Zaehlerbeginn.

## Wechselrichter und Stromnetz

| Signale | Bedeutung |
|---|---|
| `active_power`, `active_power_fast` | Momentane nutzbare Wirkleistung. Der schnelle Wert wird in einem eigenen Messpfad erfasst. |
| `reactive_power`, `power_factor` | Blindleistung und Leistungsfaktor. Sie beschreiben, wie die Anlage mit netzseitiger Leistung und Phasenlage umgeht. |
| `line_voltage_*` | Spannung zwischen zwei Netzphasen (A-B, B-C oder C-A). |
| `phase_voltage_*`, `phase_current_*` | Spannung und Strom jeder einzelnen Netzphase A, B und C. |
| `grid_frequency` | Frequenz des Stromnetzes, ueblicherweise nahe 50 Hz. |
| `efficiency` | Vom Geraet gemeldeter Wirkungsgrad. |
| `internal_temperature`, `internal_temperature_N` | Temperatur des Wechselrichters beziehungsweise verschiedener interner Messpunkte. |
| `insulation_resistance` | Isolationswiderstand auf der Gleichstromseite; ein Sicherheits-/Zustandswert. |
| `device_status`, `fault_code` | Numerische Betriebszustands- und Fehlerkennungen. Die Zahlen sind Codes, keine frei lesbaren Texte. |
| `rated_power`, `maximum_*_power`, `real_maximum_*_power` | Nenn- und Grenzleistungen des Geraets; nicht die aktuelle Leistung. |
| `total_bus_voltage`, `maximum_pv_voltage`, `minimum_pv_voltage`, `*_ground_voltage` | Interne Gleichspannungen beziehungsweise Extrem- und Erdbezugsmessungen. Diese Werte sind eher Diagnosewerte als normale Leistungsanzeigen. |

## Energie und Zeit

| Signale | Bedeutung |
|---|---|
| `daily_yield_energy`, `monthly_yield_energy`, `yearly_yield_energy` | Erzeugte Energie am heutigen Tag, im Monat oder im Jahr. |
| `accumulated_yield_energy` | Gesamterzeugung seit Beginn des Energiezaehlers. |
| `total_dc_input_energy` | Insgesamt aufgenommene Energie auf der PV-Gleichstromseite. |
| `current_hour_energy`, `previous_hour_energy`, `previous_day_energy`, `previous_month_energy`, `previous_year_energy` | Energie fuer den aktuellen oder jeweils abgeschlossenen Zeitraum. |
| `startup_time`, `shutdown_time`, `*_statistics_time` | Zeitpunkte des Starts/Stopps oder Zeitmarken der Energiestatistik. Neben dem Sekunden-Rohwert kann der Poller ein Feld mit Suffix `_utc` ausgeben. |
| `peak_active_power_today` | Hoechste bisher am heutigen Tag gemessene Wirkleistung. |

## Batterie

Signale mit `battery_` beziehen sich auf den Speicher. Bei
`battery_unit_1_` und `battery_unit_2_` ist eine konkrete Batterieeinheit
gemeint; Signale ohne Einheiten-Nummer sind meist Gesamtwerte der Anlage.

| Signalgruppe | Bedeutung |
|---|---|
| `*_soc` | Ladezustand in Prozent (State of Charge). 0 % bedeutet leer, 100 % voll laut Geraeteanzeige. |
| `*_power` | Momentane Lade- oder Entladeleistung in Watt. Das Vorzeichen kann die Richtung angeben; die genaue Konvention ist geraete-/registerabhaengig. |
| `*_bus_voltage`, `*_bus_current` | Elektrische Spannung und Stromstaerke am Batterieanschluss. |
| `*_temperature`, `*_status`, `*_working_mode`, `*_fault_id` | Temperatur, Betriebszustand, Arbeitsmodus und Fehlerkennung. Codes muessen anhand der passenden Registerliste entschluesselt werden. |
| `*_daily_charge_energy`, `*_daily_discharge_energy` | Heute geladene beziehungsweise entladene Energie. |
| `*_total_charge`, `*_total_discharge`, `battery_rated_capacity` | Lebenszeit-Energiezaehler und Nennkapazitaet des Speichers. |
| `*_maximum_charge_power`, `*_maximum_discharge_power`, `*_rated_*_power` | Zulassige oder nominelle Lade- und Entladeleistung. |
| `battery_unit_*_pack_*_soh_calibration_status`, `battery_soh_*` | Status und Grenzwerte rund um die SOH-Kalibrierung. SOH steht fuer den geschaetzten Gesundheitszustand der Batterie. |

## Status-, Alarm- und Diagnosefelder

`*_alarm*`, `external_alarm_group_*`, `monitoring_alarm_group_*` und
`power_processing_status` sind Status- oder Alarmfelder. Wenn die Einheit als
`bitfield` angegeben ist, repraesentiert die Zahl mehrere einzelne
Ein/Aus-Informationen, die in Bits gepackt sind. Der Zahlenwert allein sagt
deshalb nicht, welcher Alarm aktiv ist; dafuer muss das passende Bit nach der
Registerbeschreibung ausgewertet werden.

Weitere Felder wie `remote_communication_status`,
`monitoring_running_status`, `subdevice_presence_flags`,
`upgrade_package_*_info`, `serial_number_crc` sowie Alarmzaehler und
Alarm-Seriennummern sind vor allem fuer Wartung, Kommunikation und Diagnose
gedacht. Sie sind keine direkt ablesbaren Leistungswerte.

## CSV lesen und Werte einordnen

Die CSV-Spalten bedeuten:

- `timestamp_utc`: Zeitpunkt, zu dem `sun2000Tool` den Datensatz erfasst hat.
- `signal`: stabiler technischer Feldname, zum Beispiel `active_power`.
- `value`: Messwert oder Roh-Code.
- `unit`: Einheit wie `W`, `V`, `A`, `kWh`, `%`, `code` oder `bitfield`.

Ein erfolgreicher Modbus-Zugriff bedeutet nicht automatisch, dass jeder Wert
inhaltlich sinnvoll ist. Die Firmware kann fuer nicht verfuegbare Werte
Platzhalter liefern; der Poller laesst bekannte Protokoll-Sentinelwerte weg.
Auch echte Nullwerte koennen gueltig sein, etwa 0 A bei einem gerade inaktiven
PV-Eingang. Ein fehlendes Signal kann bedeuten, dass das Register von diesem
Geraet nicht unterstuetzt wird oder nicht gelesen werden konnte.

Die Zahlen in `code`-Feldern, Alarm-Bitfeldern und Versions-IDs sind nicht
ueber alle Modelle hinweg selbsterklaerend. Fuer eine genaue Entschluesselung
ist die zur Wechselrichter- und Firmware-Version passende Huawei-Modbus-
Registerliste massgeblich. Das Tool enthaelt keine schreibenden
Modbus-Operationen.
