#pragma once

#include <fstream>
#include <functional>
#include <iostream>
#include <string>

namespace sun2000 {

enum class ConsoleOutputMode {
    summary,
    all
};

// Schreibt Messwerte in UTC-Stundendateien. Der Konstruktor erwartet einen
// Basisdateipfad (z. B. /data/sun2000.csv); write() erzeugt daraus
// sun2000_YYYY-MM-DDTHH.csv und behaelt maximal 24 passende Stunden-Dateien.
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

    // Stellt sicher, dass gepufferte Daten auf den Ausgabestrom geschrieben
    // werden; wirft bei einem Schreibfehler.
    void flush();

private:
    void open_hour(const std::string& hour_key);

    std::string base_path_;
    std::string active_hour_;
    std::ofstream stream_;
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
