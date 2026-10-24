#include "output.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sun2000 {
namespace {

// std::filesystem (C++17) behandelt Pfade plattformgerecht und wird hier fuer
// Stunden-Dateinamen, Verzeichnisdurchlauf und Ringpuffer-Bereinigung genutzt.
std::string csv_escape(const std::string& value) {
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '\0') {
            continue;
        }
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

bool is_valid_hour_key(const std::string& hour_key) {
    if (hour_key.size() != 13 || hour_key[4] != '-' ||
        hour_key[7] != '-' || hour_key[10] != 'T') {
        return false;
    }
    std::tm parsed{};
    std::istringstream input(hour_key);
    input >> std::get_time(&parsed, "%Y-%m-%dT%H");
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        return false;
    }
    parsed.tm_isdst = 0;
    const std::time_t hour = timegm(&parsed);
    std::tm verified{};
    if (gmtime_r(&hour, &verified) == nullptr) {
        return false;
    }
    std::ostringstream formatted;
    formatted << std::put_time(&verified, "%Y-%m-%dT%H");
    return formatted.str() == hour_key;
}

std::string hour_key_from_timestamp(const std::string& timestamp) {
    if (timestamp.size() != 20 || timestamp[4] != '-' ||
        timestamp[7] != '-' || timestamp[10] != 'T' ||
        timestamp[13] != ':' || timestamp[16] != ':' ||
        timestamp[19] != 'Z') {
        throw std::invalid_argument(
            "CSV-Zeitstempel muss im UTC-Format YYYY-MM-DDTHH:MM:SSZ vorliegen.");
    }
    std::tm parsed{};
    std::istringstream input(timestamp);
    input >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%SZ");
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        throw std::invalid_argument("Ungueltiger UTC-Zeitstempel fuer CSV.");
    }
    parsed.tm_isdst = 0;
    const std::time_t timestamp_value = timegm(&parsed);
    std::tm verified{};
    if (gmtime_r(&timestamp_value, &verified) == nullptr) {
        throw std::invalid_argument("Ungueltiger UTC-Zeitstempel fuer CSV.");
    }
    std::ostringstream formatted;
    formatted << std::put_time(&verified, "%Y-%m-%dT%H:%M:%SZ");
    if (formatted.str() != timestamp) {
        throw std::invalid_argument("Ungueltiger UTC-Zeitstempel fuer CSV.");
    }
    return timestamp.substr(0, 13);
}

std::time_t parse_hour_key(const std::string& hour_key) {
    std::tm parsed{};
    std::istringstream input(hour_key);
    input >> std::get_time(&parsed, "%Y-%m-%dT%H");
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        throw std::invalid_argument("Ungueltiger Stundenanteil im CSV-Dateinamen.");
    }
    parsed.tm_isdst = 0;
    return timegm(&parsed);
}

std::string previous_hour_key(const std::string& hour_key) {
    const std::time_t previous_hour = parse_hour_key(hour_key) - 60 * 60;
    std::tm utc_time{};
    if (gmtime_r(&previous_hour, &utc_time) == nullptr) {
        throw std::runtime_error("Vorherige UTC-Stunde kann nicht berechnet werden.");
    }
    std::ostringstream formatted;
    formatted << std::put_time(&utc_time, "%Y-%m-%dT%H");
    return formatted.str();
}

std::filesystem::path hourly_file_path(const std::string& base_path,
                                       const std::string& hour_key) {
    const std::filesystem::path base(base_path);
    const std::string filename =
        base.stem().string() + "_" + hour_key + base.extension().string();
    return base.parent_path() / filename;
}

void remove_expired_hourly_files(const std::string& base_path,
                                 const std::string& current_hour_key) {
    const std::filesystem::path base(base_path);
    const std::filesystem::path directory =
        base.parent_path().empty() ? std::filesystem::path(".")
                                   : base.parent_path();
    const std::string expected_prefix = base.stem().string() + "_";
    const std::string extension = base.extension().string();
    const std::time_t current_hour = parse_hour_key(current_hour_key);
    const std::time_t oldest_retained_hour = current_hour - 23 * 60 * 60;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.size() != expected_prefix.size() + 13 + extension.size() ||
            filename.compare(0, expected_prefix.size(), expected_prefix) != 0 ||
            filename.compare(filename.size() - extension.size(),
                             extension.size(), extension) != 0) {
            continue;
        }
        const std::string hour_key =
            filename.substr(expected_prefix.size(), 13);
        if (!is_valid_hour_key(hour_key)) {
            continue;
        }
        const std::time_t file_hour = parse_hour_key(hour_key);
        if (file_hour < oldest_retained_hour || file_hour > current_hour) {
            std::error_code error;
            std::filesystem::remove(entry.path(), error);
            if (error) {
                throw std::runtime_error(
                    "Alte CSV-Stundendatei kann nicht geloescht werden: " +
                    entry.path().string() + ": " + error.message());
            }
        }
    }
}

bool has_prefix(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool is_pv_summary_signal(const std::string& signal) {
    if (signal.rfind("pv_", 0) == 0) {
        const auto suffix_position = signal.find('_', 3);
        if (suffix_position != std::string::npos &&
            suffix_position > 3 &&
            (signal.compare(suffix_position, std::string::npos, "_voltage") ==
                 0 ||
             signal.compare(suffix_position, std::string::npos, "_current") ==
                 0)) {
            const auto string_number =
                signal.substr(3, suffix_position - 3);
            return !string_number.empty() &&
                   std::all_of(string_number.begin(), string_number.end(),
                               [](unsigned char character) {
                                   return character >= '0' &&
                                          character <= '9';
                               });
        }
    }
    return false;
}

bool is_summary_signal(const std::string& signal) {
    // C++17 CTAD (Template Argument Deduction) leitet den Array-Typ aus den
    // Elementen ab; der Typ muss hier nicht explizit als std::array<...>
    // ausgeschrieben werden.
    static constexpr std::array summary_signals{
        "model_name",
        "model_name_individual",
        "serial_number",
        "product_code",
        "firmware_version",
        "model_id",
        "input_power",
        "line_voltage_a_b",
        "line_voltage_b_c",
        "line_voltage_c_a",
        "phase_voltage_a",
        "phase_voltage_b",
        "phase_voltage_c",
        "phase_current_a",
        "phase_current_b",
        "phase_current_c",
        "active_power",
        "reactive_power",
        "power_factor",
        "grid_frequency",
        "efficiency",
        "internal_temperature",
        "device_status",
        "fault_code",
        "daily_yield_energy",
        "battery_unit_1_serial_number",
        "battery_unit_1_status",
        "battery_unit_1_power",
        "battery_unit_1_bus_voltage",
        "battery_unit_1_soc",
        "battery_unit_1_temperature",
        "battery_soc",
        "battery_power",
        "battery_status",
        "battery_bus_voltage",
        "battery_bus_current",
        "battery_daily_charge_energy",
        "battery_daily_discharge_energy",
    };
    return is_pv_summary_signal(signal) ||
           std::find(summary_signals.begin(), summary_signals.end(), signal) !=
               summary_signals.end();
}

std::optional<std::pair<double, double>> summary_numeric_range(
    const std::string& signal) {
    // std::optional (C++17) unterscheidet "kein Zahlenbereich bekannt" von
    // einem vorhandenen Bereich, ohne zusaetzliche Statusvariable.
    if (is_pv_summary_signal(signal)) {
        return has_prefix(signal, "pv_") &&
                       signal.find("_voltage") != std::string::npos
                   ? std::make_pair(0.0, 1500.0)
                   : std::make_pair(0.0, 500.0);
    }
    if (signal == "model_id" || signal == "device_status" ||
        signal == "fault_code" || signal == "battery_unit_1_status" ||
        signal == "battery_status") {
        return std::make_pair(0.0, 65534.0);
    }
    if (signal == "line_voltage_a_b" || signal == "line_voltage_b_c" ||
        signal == "line_voltage_c_a" || signal == "phase_voltage_a" ||
        signal == "phase_voltage_b" || signal == "phase_voltage_c" ||
        signal == "battery_unit_1_bus_voltage" ||
        signal == "battery_bus_voltage") {
        return std::make_pair(0.0, 2000.0);
    }
    if (signal == "phase_current_a" || signal == "phase_current_b" ||
        signal == "phase_current_c") {
        return std::make_pair(0.0, 2000.0);
    }
    if (signal == "battery_bus_current") {
        return std::make_pair(-2000.0, 2000.0);
    }
    if (signal == "input_power" || signal == "daily_yield_energy" ||
        signal == "battery_daily_charge_energy" ||
        signal == "battery_daily_discharge_energy") {
        return std::make_pair(0.0, 1000000.0);
    }
    if (signal == "active_power" || signal == "reactive_power" ||
        signal == "battery_unit_1_power" || signal == "battery_power") {
        return std::make_pair(-200000.0, 200000.0);
    }
    if (signal == "power_factor") {
        return std::make_pair(-1.0, 1.0);
    }
    if (signal == "grid_frequency") {
        return std::make_pair(40.0, 70.0);
    }
    if (signal == "efficiency") {
        return std::make_pair(0.0, 100.0);
    }
    if (signal == "internal_temperature" ||
        signal == "battery_unit_1_temperature") {
        return std::make_pair(-40.0, 150.0);
    }
    if (signal == "battery_unit_1_soc" || signal == "battery_soc") {
        return std::make_pair(0.0, 100.0);
    }
    return std::nullopt;
}

bool is_valid_summary_value(const std::string& signal,
                            const std::string& value) {
    if (!is_summary_signal(signal) || value.empty() || value == "N/A") {
        return false;
    }
    const auto range = summary_numeric_range(signal);
    if (!range) {
        return true;
    }

    std::istringstream input(value);
    double numeric_value = 0;
    input >> numeric_value;
    return input && input.peek() == std::char_traits<char>::eof() &&
           std::isfinite(numeric_value) && numeric_value >= range->first &&
           numeric_value <= range->second;
}

}  // namespace

CsvWriter::CsvWriter(const std::string& path, BeforeHourOpen before_hour_open)
    : base_path_(path), before_hour_open_(std::move(before_hour_open)) {
    if (base_path_.empty() || std::filesystem::path(base_path_).filename().empty()) {
        throw std::invalid_argument("CSV-Basispfad muss einen Dateinamen enthalten.");
    }
}

void CsvWriter::open_hour(const std::string& hour_key) {
    if (stream_.is_open()) {
        stream_.flush();
        if (!stream_) {
            throw std::runtime_error("Vor Stundenwechsel konnte CSV nicht geschrieben werden.");
        }
        stream_.close();
    }
    if (before_hour_open_) {
        const std::string hour_to_archive =
            active_hour_.empty() ? previous_hour_key(hour_key) : active_hour_;
        const auto previous_path = hourly_file_path(base_path_, hour_to_archive);
        if (std::filesystem::is_regular_file(previous_path)) {
            before_hour_open_(previous_path.string());
        }
    }
    const auto path = hourly_file_path(base_path_, hour_key);
    const bool needs_header =
        !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    stream_.open(path, std::ios::app);
    if (!stream_) {
        throw std::runtime_error("Logdatei kann nicht geoeffnet werden: " +
                                 path.string());
    }
    if (needs_header) {
        stream_ << "timestamp_utc,signal,value,unit\n";
        if (!stream_) {
            throw std::runtime_error(
                "CSV-Kopfzeile konnte nicht geschrieben werden: " +
                path.string());
        }
    }
    active_hour_ = hour_key;
    remove_expired_hourly_files(base_path_, active_hour_);
}

void CsvWriter::write(const std::string& timestamp, const std::string& signal,
                      const std::string& value, const std::string& unit) {
    const std::string hour_key = hour_key_from_timestamp(timestamp);
    if (hour_key != active_hour_ || !stream_.is_open()) {
        open_hour(hour_key);
    }
    stream_ << timestamp << ',' << csv_escape(signal) << ','
            << csv_escape(value) << ',' << csv_escape(unit) << '\n';
    if (!stream_) {
        throw std::runtime_error("CSV-Datensatz konnte nicht geschrieben werden.");
    }
}

void CsvWriter::flush() {
    if (!stream_.is_open()) {
        return;
    }
    stream_.flush();
    if (!stream_) {
        throw std::runtime_error("CSV-Datei konnte nicht geschrieben werden.");
    }
}

ConsoleReporter::ConsoleReporter(std::ostream& output, ConsoleOutputMode mode,
                                 std::ostream& diagnostics)
    : output_(output), diagnostics_(diagnostics), mode_(mode) {}

void ConsoleReporter::begin_poll(const std::string& timestamp) {
    output_ << "SUN2000-Messwerte (" << timestamp << " UTC)\n";
}

void ConsoleReporter::section(const std::string& title) {
    output_ << '\n' << title << '\n';
}

void ConsoleReporter::value(const std::string& signal,
                            const std::string& label,
                            const std::string& value,
                            const std::string& unit) {
    if (mode_ == ConsoleOutputMode::summary &&
        !is_valid_summary_value(signal, value)) {
        return;
    }
    output_ << "  " << std::left << std::setw(32) << label << "  " << value;
    if (!unit.empty()) {
        output_ << ' ' << unit;
    }
    output_ << '\n';
}

void ConsoleReporter::diagnostic(const std::string& message) {
    if (mode_ == ConsoleOutputMode::all) {
        diagnostics_ << message << '\n';
    }
}

void ConsoleReporter::finish_poll() {
    output_ << "\nAbfrage abgeschlossen.\n";
}

}  // namespace sun2000
