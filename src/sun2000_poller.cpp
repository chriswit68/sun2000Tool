#include "sun2000_poller.hpp"

#include "modbus_client.hpp"
#include "register_decoder.hpp"
#include "register_map.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sun2000 {
namespace {

struct Metric {
    std::string name;
    std::string unit;
    int address;
    RegisterFormat format;
    double scale;
    bool epoch_timestamp{};
};

struct StringMetric {
    const char* signal;
    const char* label;
    int address;
    int register_count;
};

// Zentrale Liste der festen Stringregister: Signalname, Konsolenlabel,
// Startadresse und Registerlaenge bleiben so pro Messwert zusammen.
const std::array<StringMetric, 19> string_metrics{{
    {"serial_number", "Seriennummer", registers::inverter_serial_number,
     registers::inverter_serial_number_register_count},
    {"product_code", "Produktcode", registers::inverter_product_code,
     registers::inverter_product_code_register_count},
    {"firmware_version", "Firmware", registers::inverter_firmware_version,
     registers::inverter_firmware_version_register_count},
    {"software_version", "Softwareversion",
     registers::inverter_software_version,
     registers::inverter_software_version_register_count},
    {"product_sales_area", "Produkt-Verkaufsregion",
     registers::inverter_product_sales_area,
     registers::inverter_product_sales_area_register_count},
    {"third_party_serial_number", "Drittanbieter-Seriennummer",
     registers::inverter_third_party_serial_number,
     registers::inverter_third_party_serial_number_register_count},
    {"hardware_version", "Hardwareversion", registers::inverter_hardware_version,
     registers::inverter_hardware_version_register_count},
    {"monitoring_board_serial_number", "Seriennummer Monitoring-Platine",
     registers::inverter_monitoring_board_serial_number,
     registers::inverter_monitoring_board_serial_number_register_count},
    {"monitoring_software_version", "Monitoring-Softwareversion",
     registers::inverter_monitoring_software_version,
     registers::inverter_monitoring_software_version_register_count},
    {"main_dsp_version", "Haupt-DSP-Version", registers::inverter_main_dsp_version,
     registers::inverter_main_dsp_version_register_count},
    {"cpld_version", "CPLD-Version", registers::inverter_cpld_version,
     registers::inverter_cpld_version_register_count},
    {"afci_version", "AFCI-Version", registers::inverter_afci_version,
     registers::inverter_afci_version_register_count},
    {"registration_code", "Registrierungscode",
     registers::inverter_registration_code,
     registers::inverter_registration_code_register_count},
    {"battery_unit_1_dcdc_version", "Batterie DCDC-Version",
     registers::battery_unit_1_dcdc_version,
     registers::battery_unit_1_dcdc_version_register_count},
    {"battery_unit_1_bms_version", "Batterie BMS-Version",
     registers::battery_unit_1_bms_version,
     registers::battery_unit_1_bms_version_register_count},
    {"battery_unit_1_serial_number", "Batterie 1 Seriennummer",
     registers::battery_unit_1_serial_number,
     registers::battery_unit_1_serial_number_register_count},
    {"battery_unit_2_serial_number", "Batterie 2 Seriennummer",
     registers::battery_unit_2_serial_number,
     registers::battery_unit_2_serial_number_register_count},
    {"battery_unit_2_software_version", "Batterie 2 Softwareversion",
     registers::battery_unit_2_software_version,
     registers::battery_unit_2_software_version_register_count},
    {"battery_unit_1_software_version", "Batterie 1 Softwareversion",
     registers::battery_unit_1_software_version,
     registers::battery_unit_1_software_version_register_count},
}};

std::string timestamp_utc() {
    const std::time_t current_time = std::time(nullptr);
    std::tm utc_time{};
    gmtime_r(&current_time, &utc_time);
    std::ostringstream timestamp;
    timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return timestamp.str();
}

std::optional<std::string> format_epoch_utc(std::uint32_t epoch_seconds) {
    // std::optional (C++17) signalisiert, dass Sentinel- oder nicht
    // darstellbare Zeitwerte kein formatiertes Datum liefern.
    if (epoch_seconds == 0 ||
        epoch_seconds == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    const std::time_t epoch_time = static_cast<std::time_t>(epoch_seconds);
    std::tm utc_time{};
    if (gmtime_r(&epoch_time, &utc_time) == nullptr) {
        return std::nullopt;
    }

    std::ostringstream formatted;
    formatted << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return formatted.str();
}

void report_value(const std::string& timestamp, const std::string& signal,
                  const std::string& label, const std::string& value,
                  const std::string& unit, CsvWriter* csv,
                  ConsoleReporter& console) {
    if (csv != nullptr) {
        csv->write(timestamp, signal, value, unit);
    }
    console.value(signal, label, value, unit);
}

void read_and_report_metric(ModbusClient& client, const Metric& metric,
                            const std::string& timestamp, CsvWriter* csv,
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
        console.diagnostic("Metrik " + metric.name + ": " + error.what());
        return;
    }

    const auto decoded =
        decode_register_value(registers, metric.format, metric.scale);
    if (!decoded) {
        return;
    }
    const double value = *decoded;
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(6) << value;
    report_value(timestamp, metric.name, metric.name, formatted.str(),
                 metric.unit, csv, console);

    if (metric.epoch_timestamp) {
        const std::uint32_t epoch_seconds =
            (static_cast<std::uint32_t>(registers[0]) << 16) | registers[1];
        const auto utc_timestamp = format_epoch_utc(epoch_seconds);
        const std::string utc_signal = metric.name + "_utc";
        const std::string utc_value =
            utc_timestamp.value_or("N/A");
        report_value(timestamp, utc_signal, metric.name + " (UTC)", utc_value,
                     "UTC", csv, console);
    }
}

void read_and_report_string(ModbusClient& client, int address,
                            int register_count, const std::string& signal,
                            const std::string& label,
                            const std::string& timestamp, CsvWriter* csv,
                            ConsoleReporter& console) {
    std::string value;
    try {
        value = client.read_string(address, register_count);
    } catch (const std::exception& error) {
        console.diagnostic(error.what());
        return;
    }
    if (!value.empty()) {
        report_value(timestamp, signal, label, value, "", csv, console);
    }
}

}  // namespace

Sun2000Poller::Sun2000Poller(PollConnection connection, CsvWriter* csv,
                             ConsoleReporter& console)
    : connection_(std::move(connection)), csv_(csv), console_(console) {}

PollResult Sun2000Poller::poll_once() {
    PollResult result;
    std::unique_ptr<ModbusClient> client_owner;
    try {
        client_owner = std::make_unique<ModbusClient>(
            connection_.host, connection_.port, connection_.unit_id);
        ModbusClient& client = *client_owner;
        result.connected = true;
        const std::string timestamp = timestamp_utc();
        console_.begin_poll(timestamp);

        console_.section("Identifikation");
        std::string model;
        bool model_read_individually = false;
        try {
            model = client.read_string(
                registers::inverter_model_name,
                registers::inverter_model_name_register_count);
        } catch (const std::exception& error) {
            console_.diagnostic("Modell (Blockzugriff): " +
                                std::string(error.what()));
            try {
                model = client.read_string_register_by_register(
                    registers::inverter_model_name,
                    registers::inverter_model_name_register_count);
                model_read_individually = true;
                if (model.empty()) {
                    console_.diagnostic("Modell (Einzelregister): leere Antwort");
                }
            } catch (const std::exception& individual_error) {
                console_.diagnostic("Modell (Einzelregister): " +
                                    std::string(individual_error.what()));
            }
        }
        if (!model.empty()) {
            const std::string signal =
                model_read_individually ? "model_name_individual" : "model_name";
            const std::string label = model_read_individually
                                          ? "Modell (Einzelregister)"
                                          : "Modell";
            report_value(timestamp, signal, label, model, "", csv_, console_);
        }
        for (const auto& metric : string_metrics) {
            read_and_report_string(client, metric.address, metric.register_count,
                                   metric.signal, metric.label, timestamp, csv_,
                                   console_);
        }

        std::optional<std::uint16_t> model_id;
        try {
            model_id =
                client.read_holding_registers(registers::inverter_model_id, 1)[0];
        } catch (const std::exception& error) {
            console_.diagnostic("Modell-ID: " + std::string(error.what()));
        }
        if (model_id) {
            report_value(timestamp, "model_id", "Modell-ID",
                         std::to_string(*model_id), "", csv_, console_);
        }

        console_.section("PV-Eingaenge");
        std::optional<std::uint16_t> pv_string_count;
        try {
            pv_string_count =
                client.read_holding_registers(registers::inverter_pv_string_count,
                                              1)[0];
        } catch (const std::exception& error) {
            console_.diagnostic("PV-String-Anzahl: " +
                                std::string(error.what()));
        }
        if (pv_string_count) {
            const auto bounded_count =
                std::min<std::uint16_t>(
                    *pv_string_count,
                    registers::maximum_pv_strings_in_register_map);
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

        static const std::vector<Metric> additional_inverter_metrics{
            {"mppt_count", "", registers::inverter_mppt_count,
             RegisterFormat::unsigned16, 1},
            {"rated_power", "kW", registers::inverter_rated_power,
             RegisterFormat::unsigned32, 1000},
            {"maximum_active_power", "kW",
             registers::inverter_maximum_active_power,
             RegisterFormat::unsigned32, 1000},
            {"maximum_apparent_power", "kVA",
             registers::inverter_maximum_apparent_power,
             RegisterFormat::unsigned32, 1000},
            {"maximum_reactive_power_export", "kvar",
             registers::inverter_maximum_reactive_power_export,
             RegisterFormat::signed32, 1000},
            {"maximum_reactive_power_import", "kvar",
             registers::inverter_maximum_reactive_power_import,
             RegisterFormat::signed32, 1000},
            {"real_maximum_active_power", "kW",
             registers::inverter_real_maximum_active_power,
             RegisterFormat::unsigned32, 1000},
            {"real_maximum_apparent_power", "kVA",
             registers::inverter_real_maximum_apparent_power,
             RegisterFormat::unsigned32, 1000},
            {"product_software_id", "", registers::inverter_product_software_id,
             RegisterFormat::unsigned16, 1},
            {"product_software_version_number", "",
             registers::inverter_product_software_version_number,
             RegisterFormat::unsigned16, 1},
            {"grid_code_protocol_version", "",
             registers::inverter_grid_code_protocol_version,
             RegisterFormat::unsigned16, 1},
            {"modbus_protocol_version", "",
             registers::inverter_modbus_protocol_version,
             RegisterFormat::unsigned32, 1},
            {"software_id", "", registers::inverter_software_id,
             RegisterFormat::unsigned16, 1},
            {"third_party_model_id", "",
             registers::inverter_third_party_model_id,
             RegisterFormat::unsigned16, 1},
            {"subdevice_support_id", "",
             registers::inverter_subdevice_support_id,
             RegisterFormat::unsigned32, 1},
            {"subdevice_presence_flags", "",
             registers::inverter_subdevice_presence_flags,
             RegisterFormat::unsigned32, 1},
            {"remote_communication_status", "bitfield",
             registers::inverter_remote_communication_status,
             RegisterFormat::unsigned16, 1},
            {"monitoring_running_status", "bitfield",
             registers::inverter_monitoring_running_status,
             RegisterFormat::unsigned16, 1},
            {"power_processing_status", "bitfield",
             registers::inverter_power_processing_status,
             RegisterFormat::unsigned32, 1},
            {"monitoring_alarm_1", "bitfield",
             registers::inverter_monitoring_alarm_1,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_2", "bitfield",
             registers::inverter_monitoring_alarm_2,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_3", "bitfield",
             registers::inverter_monitoring_alarm_3,
             RegisterFormat::unsigned16, 1},
            {"serial_number_crc", "",
             registers::inverter_serial_number_crc,
             RegisterFormat::unsigned16, 1},
            {"peak_active_power_today", "kW",
             registers::inverter_peak_active_power_today,
             RegisterFormat::signed32, 1000},
            {"startup_time", "s since epoch", registers::inverter_startup_time,
             RegisterFormat::unsigned32, 1, true},
            {"shutdown_time", "s since epoch",
             registers::inverter_shutdown_time,
             RegisterFormat::unsigned32, 1, true},
            {"total_dc_input_energy", "kWh",
             registers::inverter_total_dc_input_energy,
             RegisterFormat::unsigned32, 100},
            {"energy_statistics_time", "s since epoch",
             registers::inverter_energy_statistics_time,
             RegisterFormat::unsigned32, 1, true},
            {"current_hour_energy", "kWh",
             registers::inverter_current_hour_energy,
             RegisterFormat::unsigned32, 100},
            {"yearly_yield_energy", "kWh",
             registers::inverter_yearly_yield_energy,
             RegisterFormat::unsigned32, 100},
            {"critical_alarm_count", "",
             registers::inverter_critical_alarm_count,
             RegisterFormat::unsigned16, 1},
            {"major_alarm_count", "", registers::inverter_major_alarm_count,
             RegisterFormat::unsigned16, 1},
            {"minor_alarm_count", "", registers::inverter_minor_alarm_count,
             RegisterFormat::unsigned16, 1},
            {"warning_alarm_count", "",
             registers::inverter_warning_alarm_count,
             RegisterFormat::unsigned16, 1},
            {"alarm_clear_serial_number", "",
             registers::inverter_alarm_clear_serial_number,
             RegisterFormat::unsigned16, 1},
            {"previous_hour_statistics_time", "s since epoch",
             registers::inverter_previous_hour_statistics_time,
             RegisterFormat::unsigned32, 1, true},
            {"previous_hour_energy", "kWh",
             registers::inverter_previous_hour_energy,
             RegisterFormat::unsigned32, 100},
            {"previous_day_statistics_time", "s since epoch",
             registers::inverter_previous_day_statistics_time,
             RegisterFormat::unsigned32, 1, true},
            {"previous_day_energy", "kWh",
             registers::inverter_previous_day_energy,
             RegisterFormat::unsigned32, 100},
            {"previous_month_statistics_time", "s since epoch",
             registers::inverter_previous_month_statistics_time,
             RegisterFormat::unsigned32, 1, true},
            {"previous_month_energy", "kWh",
             registers::inverter_previous_month_energy,
             RegisterFormat::unsigned32, 100},
            {"previous_year_statistics_time", "s since epoch",
             registers::inverter_previous_year_statistics_time,
             RegisterFormat::unsigned32, 1, true},
            {"previous_year_energy", "kWh",
             registers::inverter_previous_year_energy,
             RegisterFormat::unsigned32, 100},
            {"latest_active_alarm_serial_number", "",
             registers::inverter_latest_active_alarm_serial_number,
             RegisterFormat::unsigned32, 1},
            {"latest_historical_alarm_serial_number", "",
             registers::inverter_latest_historical_alarm_serial_number,
             RegisterFormat::unsigned32, 1},
            {"total_bus_voltage", "V", registers::inverter_total_bus_voltage,
             RegisterFormat::signed16, 10},
            {"maximum_pv_voltage", "V",
             registers::inverter_maximum_pv_voltage,
             RegisterFormat::signed16, 10},
            {"minimum_pv_voltage", "V",
             registers::inverter_minimum_pv_voltage,
             RegisterFormat::signed16, 10},
            {"average_pv_negative_ground_voltage", "V",
             registers::inverter_average_pv_negative_ground_voltage,
             RegisterFormat::signed16, 10},
            {"maximum_pv_positive_ground_voltage", "V",
             registers::inverter_maximum_pv_positive_ground_voltage,
             RegisterFormat::signed16, 10},
            {"minimum_pv_negative_ground_voltage", "V",
             registers::inverter_minimum_pv_negative_ground_voltage,
             RegisterFormat::signed16, 10},
            {"pe_withstand_voltage", "V",
             registers::inverter_pe_withstand_voltage,
             RegisterFormat::unsigned16, 1},
            {"iso_feature_information", "bitfield",
             registers::inverter_iso_feature_information,
             RegisterFormat::unsigned16, 1},
            {"builtin_pid_status", "code",
             registers::inverter_builtin_pid_status,
             RegisterFormat::unsigned16, 1},
            {"pv_negative_ground_voltage", "V",
             registers::inverter_pv_negative_ground_voltage,
             RegisterFormat::signed16, 10},
            {"builtin_pid_compensation_direction", "code",
             registers::inverter_builtin_pid_compensation_direction,
             RegisterFormat::unsigned16, 1},
            {"mppt_1_dc_total_energy", "kWh",
             registers::inverter_mppt_1_dc_total_energy,
             RegisterFormat::unsigned32, 100},
            {"mppt_2_dc_total_energy", "kWh",
             registers::inverter_mppt_1_dc_total_energy +
                 registers::inverter_mppt_dc_energy_stride,
             RegisterFormat::unsigned32, 100},
            {"mppt_3_dc_total_energy", "kWh",
             registers::inverter_mppt_1_dc_total_energy +
                 2 * registers::inverter_mppt_dc_energy_stride,
             RegisterFormat::unsigned32, 100},
            {"mppt_4_dc_total_energy", "kWh",
             registers::inverter_mppt_1_dc_total_energy +
                 3 * registers::inverter_mppt_dc_energy_stride,
             RegisterFormat::unsigned32, 100},
            {"monitoring_alarm_group_0", "bitfield",
             registers::inverter_monitoring_alarm_1_group_0,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_group_1", "bitfield",
             registers::inverter_monitoring_alarm_2_group_1,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_group_2", "bitfield",
             registers::inverter_monitoring_alarm_3_group_2,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_0", "bitfield",
             registers::inverter_external_alarm_1_group_0,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_1", "bitfield",
             registers::inverter_external_alarm_2_group_1,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_2", "bitfield",
             registers::inverter_external_alarm_3_group_2,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_3", "bitfield",
             registers::inverter_external_alarm_4_group_3,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_4", "bitfield",
             registers::inverter_external_alarm_5_group_4,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_5", "bitfield",
             registers::inverter_external_alarm_6_group_5,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_6", "bitfield",
             registers::inverter_external_alarm_7_group_6,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_7", "bitfield",
             registers::inverter_external_alarm_8_group_7,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_8", "bitfield",
             registers::inverter_external_alarm_9_group_8,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_9", "bitfield",
             registers::inverter_external_alarm_10_group_9,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_10", "bitfield",
             registers::inverter_external_alarm_11_group_10,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_11", "bitfield",
             registers::inverter_external_alarm_12_group_11,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_12", "bitfield",
             registers::inverter_external_alarm_13_group_12,
             RegisterFormat::unsigned16, 1},
            {"internal_pid_alarm", "bitfield",
             registers::inverter_internal_pid_alarm,
             RegisterFormat::unsigned16, 1},
            {"external_alarm_group_14", "bitfield",
             registers::inverter_external_alarm_15_group_14,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_group_3", "bitfield",
             registers::inverter_monitoring_alarm_4_group_3,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_group_4", "bitfield",
             registers::inverter_monitoring_alarm_5_group_4,
             RegisterFormat::unsigned16, 1},
            {"monitoring_alarm_group_16", "bitfield",
             registers::inverter_monitoring_alarm_6_group_16,
             RegisterFormat::unsigned16, 1},
            {"stent_system_status", "bitfield",
             registers::inverter_stent_system_status,
             RegisterFormat::unsigned32, 1},
            {"capacitor_bank_running_time", "h",
             registers::inverter_capacitor_bank_running_time,
             RegisterFormat::unsigned32, 10},
            {"internal_fan_1_running_time", "h",
             registers::inverter_internal_fan_1_running_time,
             RegisterFormat::unsigned32, 10},
            {"internal_alarm", "V", registers::inverter_internal_alarm,
             RegisterFormat::unsigned16, 1},
            {"internal_temperature_1", "C",
             registers::inverter_internal_temperature_1,
             RegisterFormat::signed16, 10},
            {"internal_temperature_2", "C",
             registers::inverter_internal_temperature_2,
             RegisterFormat::signed16, 10},
            {"internal_temperature_3", "C",
             registers::inverter_internal_temperature_3,
             RegisterFormat::signed16, 10},
            {"internal_temperature_4", "C",
             registers::inverter_internal_temperature_4,
             RegisterFormat::signed16, 10},
            {"internal_temperature_5", "C",
             registers::inverter_internal_temperature_5,
             RegisterFormat::signed16, 10},
        };
        for (const auto& metric : additional_inverter_metrics) {
            read_and_report_metric(client, metric, timestamp, csv_, console_);
        }
        std::optional<std::uint16_t> package_count;
        try {
            package_count =
                client.read_holding_registers(
                    registers::inverter_upgrade_package_count, 1)[0];
        } catch (const std::exception& error) {
            console_.diagnostic("Upgrade-Paketanzahl: " +
                                std::string(error.what()));
        }
        if (package_count) {
            report_value(timestamp, "upgrade_package_count",
                         "Upgrade-Paketanzahl", std::to_string(*package_count),
                         "", csv_, console_);
            const auto bounded_package_count =
                std::min<std::uint16_t>(
                    *package_count,
                    registers::inverter_upgrade_package_max_count);
            for (std::uint16_t index = 0; index < bounded_package_count;
                 ++index) {
                const int address =
                    registers::inverter_upgrade_package_info_1 +
                    static_cast<int>(index) *
                        registers::inverter_upgrade_package_info_stride;
                read_and_report_metric(
                    client,
                    {"upgrade_package_" + std::to_string(index + 1) + "_info",
                     "", address, RegisterFormat::unsigned32, 1},
                    timestamp, csv_, console_);
            }
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

        static const std::vector<Metric> additional_battery_metrics{
            {"battery_unit_1_rated_charge_power", "W",
             registers::battery_unit_1_rated_charge_power,
             RegisterFormat::unsigned32, 1},
            {"battery_unit_1_rated_discharge_power", "W",
             registers::battery_unit_1_rated_discharge_power,
             RegisterFormat::unsigned32, 1},
            {"battery_unit_1_fault_id", "code",
             registers::battery_unit_1_fault_id,
             RegisterFormat::unsigned16, 1},
            {"battery_unit_1_daily_charge_energy", "kWh",
             registers::battery_unit_1_daily_charge_energy,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_1_daily_discharge_energy", "kWh",
             registers::battery_unit_1_daily_discharge_energy,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_1_bus_current", "A",
             registers::battery_unit_1_bus_current,
             RegisterFormat::signed16, 10},
            {"battery_unit_1_remaining_time", "min",
             registers::battery_unit_1_remaining_time,
             RegisterFormat::unsigned16, 1},
            {"battery_maximum_charge_power", "W",
             registers::battery_maximum_charge_power,
             RegisterFormat::unsigned32, 1},
            {"battery_maximum_discharge_power", "W",
             registers::battery_maximum_discharge_power,
             RegisterFormat::unsigned32, 1},
            {"battery_unit_1_total_charge", "kWh",
             registers::battery_unit_1_total_charge,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_1_total_discharge", "kWh",
             registers::battery_unit_1_total_discharge,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_2_soc", "%",
             registers::battery_unit_2_soc,
             RegisterFormat::unsigned16, 10},
            {"battery_unit_2_status", "code",
             registers::battery_unit_2_status,
             RegisterFormat::unsigned16, 1},
            {"battery_unit_2_power", "W",
             registers::battery_unit_2_power,
             RegisterFormat::signed32, 1},
            {"battery_unit_2_daily_charge_energy", "kWh",
             registers::battery_unit_2_daily_charge_energy,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_2_daily_discharge_energy", "kWh",
             registers::battery_unit_2_daily_discharge_energy,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_2_bus_voltage", "V",
             registers::battery_unit_2_bus_voltage,
             RegisterFormat::unsigned16, 10},
            {"battery_unit_2_bus_current", "A",
             registers::battery_unit_2_bus_current,
             RegisterFormat::signed16, 10},
            {"battery_unit_2_temperature", "C",
             registers::battery_unit_2_temperature,
             RegisterFormat::signed16, 10},
            {"battery_unit_2_total_charge", "kWh",
             registers::battery_unit_2_total_charge,
             RegisterFormat::unsigned32, 100},
            {"battery_unit_2_total_discharge", "kWh",
             registers::battery_unit_2_total_discharge,
             RegisterFormat::unsigned32, 100},
            {"battery_rated_capacity", "Wh",
             registers::battery_rated_capacity,
             RegisterFormat::unsigned32, 1},
            {"battery_status", "code", registers::battery_status,
             RegisterFormat::unsigned16, 1},
            {"battery_bus_voltage", "V", registers::battery_bus_voltage,
             RegisterFormat::unsigned16, 10},
            {"battery_bus_current", "A", registers::battery_bus_current,
             RegisterFormat::signed16, 10},
            {"battery_total_discharge", "kWh",
             registers::battery_total_discharge,
             RegisterFormat::unsigned32, 100},
            {"battery_daily_charge_energy", "kWh",
             registers::battery_daily_charge_energy,
             RegisterFormat::unsigned32, 100},
            {"battery_daily_discharge_energy", "kWh",
             registers::battery_daily_discharge_energy,
             RegisterFormat::unsigned32, 100},
        };
        for (const auto& metric : additional_battery_metrics) {
            read_and_report_metric(client, metric, timestamp, csv_, console_);
        }

        const std::array<int, 6> battery_pack_soh_registers{{
            registers::battery_unit_1_pack_1_soh_calibration_status,
            registers::battery_unit_1_pack_2_soh_calibration_status,
            registers::battery_unit_1_pack_3_soh_calibration_status,
            registers::battery_unit_2_pack_1_soh_calibration_status,
            registers::battery_unit_2_pack_2_soh_calibration_status,
            registers::battery_unit_2_pack_3_soh_calibration_status,
        }};
        for (std::size_t index = 0; index < battery_pack_soh_registers.size();
             ++index) {
            const int storage_unit = index < 3 ? 1 : 2;
            const int pack_number = static_cast<int>(index % 3) + 1;
            read_and_report_metric(
                client,
                {"battery_unit_" + std::to_string(storage_unit) + "_pack_" +
                     std::to_string(pack_number) +
                     "_soh_calibration_status",
                 "code", battery_pack_soh_registers[index],
                 RegisterFormat::unsigned16, 1},
                timestamp, csv_, console_);
        }
        static const std::array<Metric, 3> battery_soh_metrics{{
            {"battery_soh_calibration_status", "code",
             registers::battery_soh_calibration_status,
             RegisterFormat::unsigned16, 1},
            {"battery_soh_lower_discharge_soc_limit", "%",
             registers::battery_soh_lower_discharge_soc_limit,
             RegisterFormat::unsigned16, 10},
            {"battery_soh_backup_power_soc", "%",
             registers::battery_soh_backup_power_soc,
             RegisterFormat::unsigned16, 10},
        }};
        for (const auto& metric : battery_soh_metrics) {
            read_and_report_metric(client, metric, timestamp, csv_, console_);
        }

        if (csv_ != nullptr) {
            csv_->flush();
        }
        console_.finish_poll();
        const auto read_stats = client.read_stats();
        result.read_attempts = read_stats.attempts;
        result.reads_succeeded = read_stats.succeeded;
        result.reads_failed = read_stats.failed;
        return result;
    } catch (const std::exception& error) {
        result.error = error.what();
        if (client_owner) {
            const auto read_stats = client_owner->read_stats();
            result.read_attempts = read_stats.attempts;
            result.reads_succeeded = read_stats.succeeded;
            result.reads_failed = read_stats.failed;
        }
        return result;
    }
}

}  // namespace sun2000
