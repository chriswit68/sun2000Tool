#include "output.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sun2000 {
namespace {

std::string csv_escape(const std::string& value) {
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
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

CsvWriter::CsvWriter(const std::string& path)
    : stream_(path, std::ios::app) {
    if (!stream_) {
        throw std::runtime_error("Logdatei kann nicht geoeffnet werden: " +
                                 path);
    }
    if (!std::filesystem::exists(path) ||
        std::filesystem::file_size(path) == 0) {
        stream_ << "timestamp_utc,signal,value,unit\n";
        if (!stream_) {
            throw std::runtime_error("CSV-Kopfzeile konnte nicht geschrieben werden: " +
                                     path);
        }
    }
}

void CsvWriter::write(const std::string& timestamp, const std::string& signal,
                      const std::string& value, const std::string& unit) {
    stream_ << timestamp << ',' << csv_escape(signal) << ','
            << csv_escape(value) << ',' << csv_escape(unit) << '\n';
    if (!stream_) {
        throw std::runtime_error("CSV-Datensatz konnte nicht geschrieben werden.");
    }
}

void CsvWriter::flush() {
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
