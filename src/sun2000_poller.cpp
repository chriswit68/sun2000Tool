#include "sun2000_poller.hpp"

#include "modbus_client.hpp"
#include "register_map.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sun2000 {
namespace {

enum class RegisterFormat {
    unsigned16,
    signed16,
    unsigned32,
    signed32
};

struct Metric {
    std::string name;
    std::string unit;
    int address;
    RegisterFormat format;
    double scale;
};

std::string timestamp_utc() {
    const std::time_t current_time = std::time(nullptr);
    std::tm utc_time{};
    gmtime_r(&current_time, &utc_time);
    std::ostringstream timestamp;
    timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return timestamp.str();
}

std::int32_t decode_signed32(std::uint32_t raw) {
    const std::int64_t value =
        raw <= 0x7FFFFFFFu ? static_cast<std::int64_t>(raw)
                           : static_cast<std::int64_t>(raw) - 0x100000000LL;
    return static_cast<std::int32_t>(value);
}

void report_value(const std::string& timestamp, const std::string& signal,
                  const std::string& label, const std::string& value,
                  const std::string& unit, CsvWriter& csv,
                  ConsoleReporter& console) {
    csv.write(timestamp, signal, value, unit);
    console.value(label, value, unit);
}

void read_and_report_metric(ModbusClient& client, const Metric& metric,
                            const std::string& timestamp, CsvWriter& csv,
                            ConsoleReporter& console) {
    const int register_count =
        metric.format == RegisterFormat::unsigned32 ||
                metric.format == RegisterFormat::signed32
            ? 2
            : 1;
    std::vector<std::uint16_t> registers;
    try {
        registers = client.read_holding_registers(metric.address, register_count);
    } catch (const std::exception& error) {
        std::cerr << "Metrik " << metric.name << ": " << error.what() << '\n';
        return;
    }

    double value = 0;
    switch (metric.format) {
        case RegisterFormat::unsigned16:
            if (registers[0] == 0x7FFF) {
                return;
            }
            value = registers[0];
            break;
        case RegisterFormat::signed16: {
            const auto signed_value = static_cast<std::int16_t>(registers[0]);
            if (registers[0] == 0x7FFF || registers[0] == 0x8000) {
                return;
            }
            value = signed_value;
            break;
        }
        case RegisterFormat::unsigned32: {
            const std::uint32_t raw =
                (static_cast<std::uint32_t>(registers[0]) << 16) | registers[1];
            if (raw == 0x7FFFFFFF) {
                return;
            }
            value = raw;
            break;
        }
        case RegisterFormat::signed32: {
            const std::uint32_t raw =
                (static_cast<std::uint32_t>(registers[0]) << 16) | registers[1];
            if (raw == 0x7FFFFFFF || raw == 0x80000000) {
                return;
            }
            value = decode_signed32(raw);
            break;
        }
    }

    value /= metric.scale;
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(6) << value;
    report_value(timestamp, metric.name, metric.name, formatted.str(),
                 metric.unit, csv, console);
}

void read_and_report_string(ModbusClient& client, int address,
                            int register_count, const std::string& signal,
                            const std::string& label,
                            const std::string& timestamp, CsvWriter& csv,
                            ConsoleReporter& console) {
    try {
        const std::string value = client.read_string(address, register_count);
        if (!value.empty()) {
            report_value(timestamp, signal, label, value, "", csv, console);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
}

}  // namespace

Sun2000Poller::Sun2000Poller(PollConnection connection, CsvWriter& csv,
                             ConsoleReporter& console)
    : connection_(std::move(connection)), csv_(csv), console_(console) {}

bool Sun2000Poller::poll_once() {
    try {
        ModbusClient client(connection_.host, connection_.port,
                            connection_.unit_id);
        const std::string timestamp = timestamp_utc();
        console_.begin_poll(timestamp);

        console_.section("Identifikation");
        try {
            const std::string model =
                client.read_string(registers::inverter_model_name,
                                   registers::inverter_model_name_register_count);
            if (!model.empty()) {
                report_value(timestamp, "model_name", "Modell", model, "", csv_,
                             console_);
            }
        } catch (const std::exception& error) {
            std::cerr << "Modell (Blockzugriff): " << error.what() << '\n';
            try {
                const std::string model =
                    client.read_string_register_by_register(
                        registers::inverter_model_name,
                        registers::inverter_model_name_register_count);
                if (!model.empty()) {
                    report_value(timestamp, "model_name_individual",
                                 "Modell (Einzelregister)", model, "", csv_,
                                 console_);
                } else {
                    std::cerr << "Modell (Einzelregister): leere Antwort\n";
                }
            } catch (const std::exception& individual_error) {
                std::cerr << "Modell (Einzelregister): "
                          << individual_error.what() << '\n';
            }
        }
        read_and_report_string(
            client, registers::inverter_serial_number,
            registers::inverter_serial_number_register_count, "serial_number",
            "Seriennummer", timestamp, csv_, console_);
        read_and_report_string(
            client, registers::inverter_firmware_version,
            registers::inverter_firmware_version_register_count,
            "firmware_version", "Firmware", timestamp, csv_, console_);

        try {
            const auto model_id =
                client.read_holding_registers(registers::inverter_model_id, 1)[0];
            const std::string value = std::to_string(model_id);
            report_value(timestamp, "model_id", "Modell-ID", value, "", csv_,
                         console_);
        } catch (const std::exception& error) {
            std::cerr << "Modell-ID: " << error.what() << '\n';
        }

        console_.section("PV-Eingaenge");
        try {
            const auto pv_string_count =
                client.read_holding_registers(registers::inverter_pv_string_count,
                                              1)[0];
            const auto bounded_count =
                std::min<std::uint16_t>(pv_string_count, 24);
            for (std::uint16_t index = 0; index < bounded_count; ++index) {
                const int address =
                    registers::pv_string_1_voltage +
                    static_cast<int>(index) *
                        registers::pv_string_register_stride;
                const std::string suffix = std::to_string(index + 1);
                read_and_report_metric(
                    client,
                    {"pv_" + suffix + "_voltage", "V", address,
                     RegisterFormat::signed16, 10},
                    timestamp, csv_, console_);
                read_and_report_metric(
                    client,
                    {"pv_" + suffix + "_current", "A", address + 1,
                     RegisterFormat::signed16, 100},
                    timestamp, csv_, console_);
            }
        } catch (const std::exception& error) {
            std::cerr << "PV-String-Anzahl: " << error.what() << '\n';
        }

        static const std::array<Metric, 23> inverter_metrics{{
            {"input_power", "W", registers::inverter_input_power,
             RegisterFormat::signed32, 1},
            {"line_voltage_a_b", "V", registers::inverter_line_voltage_ab,
             RegisterFormat::unsigned16, 10},
            {"line_voltage_b_c", "V", registers::inverter_line_voltage_bc,
             RegisterFormat::unsigned16, 10},
            {"line_voltage_c_a", "V", registers::inverter_line_voltage_ca,
             RegisterFormat::unsigned16, 10},
            {"phase_voltage_a", "V", registers::inverter_phase_voltage_a,
             RegisterFormat::unsigned16, 10},
            {"phase_voltage_b", "V", registers::inverter_phase_voltage_b,
             RegisterFormat::unsigned16, 10},
            {"phase_voltage_c", "V", registers::inverter_phase_voltage_c,
             RegisterFormat::unsigned16, 10},
            {"phase_current_a", "A", registers::inverter_phase_current_a,
             RegisterFormat::signed32, 1000},
            {"phase_current_b", "A", registers::inverter_phase_current_b,
             RegisterFormat::signed32, 1000},
            {"phase_current_c", "A", registers::inverter_phase_current_c,
             RegisterFormat::signed32, 1000},
            {"active_power", "W", registers::inverter_active_power,
             RegisterFormat::signed32, 1},
            {"reactive_power", "var", registers::inverter_reactive_power,
             RegisterFormat::signed32, 1},
            {"power_factor", "", registers::inverter_power_factor,
             RegisterFormat::signed16, 1000},
            {"grid_frequency", "Hz", registers::inverter_grid_frequency,
             RegisterFormat::unsigned16, 100},
            {"efficiency", "%", registers::inverter_efficiency,
             RegisterFormat::unsigned16, 100},
            {"internal_temperature", "C",
             registers::inverter_internal_temperature,
             RegisterFormat::signed16, 10},
            {"insulation_resistance", "MOhm",
             registers::inverter_insulation_resistance,
             RegisterFormat::unsigned16, 1000},
            {"device_status", "code", registers::inverter_device_status,
             RegisterFormat::unsigned16, 1},
            {"fault_code", "code", registers::inverter_fault_code,
             RegisterFormat::unsigned16, 1},
            {"active_power_fast", "W", registers::inverter_active_power_fast,
             RegisterFormat::signed32, 1},
            {"accumulated_yield_energy", "kWh",
             registers::inverter_accumulated_yield_energy,
             RegisterFormat::unsigned32, 100},
            {"daily_yield_energy", "kWh",
             registers::inverter_daily_yield_energy,
             RegisterFormat::unsigned32, 100},
            {"monthly_yield_energy", "kWh",
             registers::inverter_monthly_yield_energy,
             RegisterFormat::unsigned32, 100},
        }};
        console_.section("Wechselrichter und Netz");
        for (const auto& metric : inverter_metrics) {
            read_and_report_metric(client, metric, timestamp, csv_, console_);
        }

        static const std::array<Metric, 9> battery_metrics{{
            {"battery_unit_1_status", "code", registers::battery_unit_1_status,
             RegisterFormat::unsigned16, 1},
            {"battery_unit_1_power", "W", registers::battery_unit_1_power,
             RegisterFormat::signed32, 1},
            {"battery_unit_1_bus_voltage", "V",
             registers::battery_unit_1_bus_voltage,
             RegisterFormat::unsigned16, 10},
            {"battery_unit_1_soc", "%", registers::battery_unit_1_soc,
             RegisterFormat::unsigned16, 10},
            {"battery_unit_1_working_mode", "code",
             registers::battery_unit_1_working_mode,
             RegisterFormat::unsigned16, 1},
            {"battery_unit_1_temperature", "C",
             registers::battery_unit_1_temperature,
             RegisterFormat::signed16, 10},
            {"battery_soc", "%", registers::battery_soc,
             RegisterFormat::unsigned16, 10},
            {"battery_power", "W", registers::battery_power,
             RegisterFormat::signed32, 1},
            {"battery_total_charge", "kWh", registers::battery_total_charge,
             RegisterFormat::unsigned32, 100},
        }};
        console_.section("Batterie");
        for (const auto& metric : battery_metrics) {
            read_and_report_metric(client, metric, timestamp, csv_, console_);
        }

        csv_.flush();
        console_.finish_poll();
        return true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return false;
    }
}

}  // namespace sun2000
