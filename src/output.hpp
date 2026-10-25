#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace sun2000 {

enum class ConsoleOutputMode {
    summary,
    all
};

// Schreibt UTC-Stundendateien und unveraenderliche Poll-Snapshots. Der
// Konstruktor erwartet einen Basisdateipfad (z. B. /data/sun2000.csv).
// Stundendateien bleiben der Ringspeicher; Snapshots liegen separat fuer SMB-Leser.
class CsvWriter {
public:
    using BeforeHourOpen = std::function<void(const std::string&)>;

    // Wirft bei einem leeren oder ungueltigen Basisdateipfad.
    explicit CsvWriter(const std::string& path,
                       BeforeHourOpen before_hour_open = {});

    // timestamp muss YYYY-MM-DDTHH:MM:SSZ sein. Datei- und Zeitstempelfehler
    // werden als std::exception gemeldet.
    void write(const std::string& timestamp, const std::string& signal,
               const std::string& value, const std::string& unit);

    // Veröffentlicht alle Messwerte des Polls atomar; Leser sehen entweder
    // den vorherigen oder den vollständigen neuen Dateistand.
    void flush();

private:
    void open_hour(const std::string& hour_key);
    void publish_pending_rows();
    void publish_snapshot(const std::vector<std::string>& rows);

    std::string base_path_;
    std::string active_hour_;
    std::vector<std::string> pending_rows_;
    BeforeHourOpen before_hour_open_;
};

class ConsoleReporter {
public:
    // Ausgabe fuer Messwerte und Diagnosemeldungen getrennt konfigurierbar.
    // Im summary-Modus werden Diagnosemeldungen unterdrueckt.
    explicit ConsoleReporter(
        std::ostream& output,
        ConsoleOutputMode mode = ConsoleOutputMode::summary,
        std::ostream& diagnostics = std::cerr);

    // Beginn und Ende eines Poll-Durchlaufs sowie Gruppierung der Konsolenausgabe.
    void begin_poll(const std::string& timestamp);
    void section(const std::string& title);
    // Gibt eine Messung aus; im summary-Modus nur, wenn Signal und Wert den
    // festgelegten Summary-Filtern entsprechen.
    void value(const std::string& signal, const std::string& label,
               const std::string& value, const std::string& unit = "");
    void diagnostic(const std::string& message);
    void finish_poll();

private:
    std::ostream& output_;
    std::ostream& diagnostics_;
    ConsoleOutputMode mode_;
};

}  // namespace sun2000
