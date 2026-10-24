#include "output.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path hourly_path(const std::filesystem::path& base,
                                  const std::string& timestamp) {
    return base.parent_path() /
           (base.stem().string() + "_" + timestamp.substr(0, 13) +
            base.extension().string());
}

std::string timestamp_hours_ago(std::time_t current_hour, int hours_ago) {
    const std::time_t hour = current_hour - hours_ago * 60 * 60;
    std::tm utc_time{};
    gmtime_r(&hour, &utc_time);
    std::ostringstream timestamp;
    timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return timestamp.str();
}

void test_csv_and_console_outputs_are_separate() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("sun2000-output-test-" +
                       std::to_string(unique_suffix) + ".csv");
    const std::string timestamp = "2026-10-23T08:00:00Z";
    const auto csv_path = hourly_path(path, timestamp);
    std::ostringstream console_stream;

    {
        sun2000::CsvWriter csv(path.string());
        sun2000::ConsoleReporter console(console_stream);
        csv.write(timestamp, "model_id", "487", "");
        csv.write(timestamp, "text_value", std::string("A\0B", 3), "");
        csv.write(timestamp, "internal_alarm", "0", "code");
        csv.write(timestamp, "battery_soc", "6553.5", "%");
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

    std::ifstream csv_stream(csv_path);
    const std::string csv_contents((std::istreambuf_iterator<char>(csv_stream)),
                                   std::istreambuf_iterator<char>());
    std::filesystem::remove(csv_path);

    expect(csv_contents.find("timestamp_utc,signal,value,unit\n") == 0,
           "CSV-Kopfzeile fehlt.");
    expect(csv_contents.find("\"model_id\",\"487\"") != std::string::npos,
           "CSV-Messwert fehlt.");
    expect(csv_contents.find("\"text_value\",\"AB\"") != std::string::npos,
           "CSV-Textwert enthaelt ein ungueltiges NUL-Zeichen.");
    expect(csv_contents.find('\0') == std::string::npos,
           "CSV-Ausgabe enthaelt ein NUL-Zeichen.");
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

void test_hourly_rotation_and_same_hour_append() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("sun2000-hourly-test-" +
                            std::to_string(unique_suffix));
    std::filesystem::create_directories(directory);
    const auto base = directory / "plant.csv";
    const std::string first_hour = "2026-10-23T08:00:00Z";
    const std::string next_hour = "2026-10-23T09:00:00Z";
    const auto first_file = hourly_path(base, first_hour);
    const auto second_file = hourly_path(base, next_hour);
    std::vector<std::string> archived_files;

    {
        sun2000::CsvWriter csv(base.string());
        csv.write(first_hour, "active_power", "10", "W");
        csv.flush();
    }
    {
        sun2000::CsvWriter csv(
            base.string(), [&archived_files](const std::string& previous_file) {
                archived_files.push_back(previous_file);
            });
        csv.write("2026-10-23T08:45:00Z", "active_power", "20", "W");
        csv.write(next_hour, "active_power", "30", "W");
        csv.flush();
    }
    expect(archived_files == std::vector<std::string>{first_file.string()},
           "Beim Stundenwechsel wurde nicht genau die vorherige CSV gemeldet.");

    std::ifstream first_stream(first_file);
    const std::string first_contents(
        (std::istreambuf_iterator<char>(first_stream)),
        std::istreambuf_iterator<char>());
    std::ifstream second_stream(second_file);
    const std::string second_contents(
        (std::istreambuf_iterator<char>(second_stream)),
        std::istreambuf_iterator<char>());
    expect(first_contents.find("timestamp_utc,signal,value,unit\n") == 0,
           "Erste Stunden-Datei hat keinen CSV-Header.");
    expect(first_contents.find("\"10\"") != std::string::npos &&
               first_contents.find("\"20\"") != std::string::npos,
           "Datensaetze derselben Stunde wurden nicht angehaengt.");
    expect(first_contents.find("timestamp_utc,signal,value,unit\n",
                               first_contents.find('\n') + 1) ==
               std::string::npos,
           "CSV-Header wurde innerhalb derselben Stunde wiederholt.");
    expect(second_contents.find("\"30\"") != std::string::npos,
           "Neuer Stundenwert wurde nicht in eine neue Datei rotiert.");
    expect(std::distance(std::filesystem::directory_iterator(directory),
                         std::filesystem::directory_iterator()) == 2,
           "Es wurden unerwartete Stunden-Dateien angelegt.");
    std::filesystem::remove_all(directory);
}

void test_csv_first_open_archives_existing_previous_hour() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("sun2000-archive-start-test-" +
                            std::to_string(unique_suffix));
    std::filesystem::create_directories(directory);
    const auto base = directory / "plant.csv";
    const std::string previous_timestamp = "2026-10-23T08:00:00Z";
    const std::string current_timestamp = "2026-10-23T09:00:00Z";
    const auto previous_file = hourly_path(base, previous_timestamp);
    std::vector<std::string> archived_files;

    {
        sun2000::CsvWriter csv(base.string());
        csv.write(previous_timestamp, "active_power", "100", "W");
        csv.flush();
    }
    {
        sun2000::CsvWriter csv(
            base.string(), [&archived_files](const std::string& previous_path) {
                archived_files.push_back(previous_path);
            });
        csv.write(current_timestamp, "active_power", "200", "W");
        csv.flush();
    }
    expect(archived_files == std::vector<std::string>{previous_file.string()},
           "Erster Stundenstart hat die vorherige Datei nicht gemeldet.");
    std::filesystem::remove_all(directory);
}

void test_failed_archive_prevents_opening_next_hour_file() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("sun2000-archive-failure-test-" +
                            std::to_string(unique_suffix));
    std::filesystem::create_directories(directory);
    const auto base = directory / "plant.csv";
    const std::string first_hour = "2026-10-23T08:00:00Z";
    const std::string next_hour = "2026-10-23T09:00:00Z";
    const auto next_file = hourly_path(base, next_hour);
    {
        sun2000::CsvWriter csv(base.string());
        csv.write(first_hour, "active_power", "100", "W");
        csv.flush();
    }

    bool archive_failed = false;
    try {
        sun2000::CsvWriter csv(
            base.string(), [](const std::string&) {
                throw std::runtime_error("Simulierter Archivfehler.");
            });
        csv.write(next_hour, "active_power", "200", "W");
    } catch (const std::runtime_error&) {
        archive_failed = true;
    }
    expect(archive_failed, "Archivfehler wurde nicht weitergereicht.");
    expect(!std::filesystem::exists(next_file),
           "Neue Stunden-Datei wurde trotz Archivfehler angelegt.");
    std::filesystem::remove_all(directory);
}

void test_hourly_retention_keeps_current_and_previous_23_hours() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("sun2000-retention-test-" +
                            std::to_string(unique_suffix));
    std::filesystem::create_directories(directory);
    const auto base = directory / "plant.csv";
    const std::string current_timestamp = "2026-10-24T12:00:00Z";
    std::tm current_tm{};
    std::istringstream current_input("2026-10-24T12");
    current_input >> std::get_time(&current_tm, "%Y-%m-%dT%H");
    const std::time_t current_hour = timegm(&current_tm);

    for (int hours_ago = 0; hours_ago <= 24; ++hours_ago) {
        const auto timestamp = timestamp_hours_ago(current_hour, hours_ago);
        std::ofstream(hourly_path(base, timestamp)).put('\n');
    }
    const auto unrelated = directory / "plant-notes.csv";
    std::ofstream(unrelated).put('\n');

    {
        sun2000::CsvWriter csv(base.string());
        csv.write(current_timestamp, "active_power", "42", "W");
        csv.flush();
    }

    std::size_t matching_files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string filename = entry.path().filename().string();
        if (filename.rfind("plant_", 0) == 0 &&
            filename.size() == std::string("plant_").size() + 13 + 4 &&
            entry.path().extension() == ".csv") {
            ++matching_files;
        }
    }
    expect(matching_files == 24,
           "Ringspeicher muss aktuelle und vorherige 23 Stunden behalten.");
    expect(std::filesystem::exists(unrelated),
           "Nicht zum CSV-Schema gehoerende Datei wurde veraendert.");
    expect(!std::filesystem::exists(
               hourly_path(base, timestamp_hours_ago(current_hour, 24))),
           "Aeltere als 24 Stunden liegende Datei wurde nicht entfernt.");
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    test_csv_and_console_outputs_are_separate();
    test_all_console_output_includes_unfiltered_values();
    test_summary_suppresses_register_diagnostics();
    test_hourly_rotation_and_same_hour_append();
    test_csv_first_open_archives_existing_previous_hour();
    test_failed_archive_prevents_opening_next_hour_file();
    test_hourly_retention_keeps_current_and_previous_23_hours();
}
