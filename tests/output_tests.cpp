#include "output.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_csv_and_console_outputs_are_separate() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("sun2000-output-test-" +
                       std::to_string(unique_suffix) + ".csv");
    std::ostringstream console_stream;

    {
        sun2000::CsvWriter csv(path.string());
        sun2000::ConsoleReporter console(console_stream);
        csv.write("2026-10-23T08:00:00Z", "model_id", "487", "");
        csv.write("2026-10-23T08:00:00Z", "internal_alarm", "0", "code");
        csv.write("2026-10-23T08:00:00Z", "battery_soc", "6553.5", "%");
        csv.flush();
        console.begin_poll("2026-10-23T08:00:00Z");
        console.section("Identifikation");
        console.value("model_id", "Modell-ID", "487");
        console.value("pv_10_voltage", "pv_10_voltage", "420.8", "V");
        console.value("pv_2_current", "pv_2_current", "0.000000", "A");
        console.value("pv_2_voltage", "pv_2_voltage", "-0.100000", "V");
        console.value("battery_soc", "Batterie-Ladezustand", "6553.500000",
                      "%");
        console.value("fault_code", "Fehlercode", "0.000000", "code");
        console.value("pv_negative_ground_voltage",
                      "pv_negative_ground_voltage", "-389.3", "V");
        console.value("internal_alarm", "Interner Alarm", "0", "code");
        console.finish_poll();
    }

    std::ifstream csv_stream(path);
    const std::string csv_contents((std::istreambuf_iterator<char>(csv_stream)),
                                   std::istreambuf_iterator<char>());
    std::filesystem::remove(path);

    expect(csv_contents.find("timestamp_utc,signal,value,unit\n") == 0,
           "CSV-Kopfzeile fehlt.");
    expect(csv_contents.find("\"model_id\",\"487\"") != std::string::npos,
           "CSV-Messwert fehlt.");
    expect(console_stream.str().find("Modell-ID") != std::string::npos,
           "Konsolenwert fehlt.");
    expect(console_stream.str().find("pv_10_voltage") != std::string::npos,
           "PV-String-Wert ausserhalb der ersten vier Kanaele fehlt.");
    expect(console_stream.str().find("pv_2_current") != std::string::npos,
           "Gueltiger Nullstromwert wurde ausgeblendet.");
    expect(console_stream.str().find("Fehlercode") != std::string::npos,
           "Gueltiger Null-Fehlercode wurde ausgeblendet.");
    expect(console_stream.str().find("Batterie-Ladezustand") ==
               std::string::npos,
           "Ungueltiger Ladezustand wurde nicht ausgeblendet.");
    expect(console_stream.str().find("-0.100000 V") == std::string::npos,
           "Ungueltige negative PV-Spannung wurde nicht ausgeblendet.");
    expect(console_stream.str().find("pv_negative_ground_voltage") ==
               std::string::npos,
           "Zusammenfassung enthaelt einen nicht relevanten PV-Messwert.");
    expect(console_stream.str().find("Interner Alarm") == std::string::npos,
           "Zusammenfassung enthaelt einen nicht ausgewaehlten Wert.");
    expect(console_stream.str().find("\"model_id\"") == std::string::npos,
           "Konsolenausgabe enthaelt CSV-Datensatz.");
    expect(csv_contents.find("\"internal_alarm\",\"0\"") !=
               std::string::npos,
           "CSV-Ausgabe wurde durch den Konsolenfilter veraendert.");
    expect(csv_contents.find("\"battery_soc\",\"6553.5\"") !=
               std::string::npos,
           "CSV-Ausgabe wurde durch die Konsolenvalidierung veraendert.");
}

void test_all_console_output_includes_unfiltered_values() {
    std::ostringstream console_stream;
    std::ostringstream diagnostics;
    sun2000::ConsoleReporter console(console_stream,
                                     sun2000::ConsoleOutputMode::all,
                                     diagnostics);
    console.value("internal_alarm", "Interner Alarm", "0", "code");
    console.value("battery_soc", "Batterie-Ladezustand", "6553.500000", "%");
    console.diagnostic("Metrik internal_alarm: Illegal data address");
    expect(console_stream.str().find("Interner Alarm") != std::string::npos,
           "Vollstaendige Konsolenausgabe blendet einen Wert aus.");
    expect(console_stream.str().find("Batterie-Ladezustand") !=
               std::string::npos,
           "Vollstaendige Ausgabe filtert einen Rohwert.");
    expect(diagnostics.str().find("Illegal data address") !=
               std::string::npos,
           "Vollstaendige Ausgabe unterdrueckt Registerfehler.");
}

void test_summary_suppresses_register_diagnostics() {
    std::ostringstream console_stream;
    std::ostringstream diagnostics;
    sun2000::ConsoleReporter console(console_stream,
                                     sun2000::ConsoleOutputMode::summary,
                                     diagnostics);
    console.diagnostic("Metrik internal_alarm: Illegal data address");
    expect(diagnostics.str().empty(),
           "Zusammenfassung gibt Fehler nicht lesbarer Register aus.");
}

}  // namespace

int main() {
    test_csv_and_console_outputs_are_separate();
    test_all_console_output_includes_unfiltered_values();
    test_summary_suppresses_register_diagnostics();
}
