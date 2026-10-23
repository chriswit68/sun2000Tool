#include "network_scanner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <modbus/modbus.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace {

volatile std::sig_atomic_t keep_running = 1;

struct PollOptions {
    std::string host;
    std::string log_file{"sun2000.csv"};
    int port{502};
    int unit_id{1};
    unsigned int interval_seconds{};
    bool once{};
};


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
    //double divisor;

};

void write_row(std::ofstream& log, const std::string& ts, const std::string& key, const std::string& val, const std::string& unit);

void print_section(const std::string& title) {
    std::cout << "\n" << title << '\n';
}

void print_value(const std::string& name, const std::string& value,
                 const std::string& unit) {
    std::cout << "  " << std::left << std::setw(28) << name << value;
    if (!unit.empty()) {
        std::cout << ' ' << unit;
    }
    std::cout << '\n';
}


// Sichere Funktion zum Lesen und Validieren eines Metrik-Werts
bool read_and_log_metric(std::ofstream& log, const std::string& timestamp, modbus_t* context, const Metric& metric) {
    uint16_t tab_reg[2] = {0, 0};
    int num_regs = (metric.format == RegisterFormat::unsigned32 || metric.format == RegisterFormat::signed32) ? 2 : 1;

    const int rc =
        modbus_read_registers(context, metric.address, num_regs, tab_reg);
    if (rc != num_regs) {
        std::cerr << "Metrik " << metric.name << " (Register "
                  << metric.address << ") nicht lesbar: ";
        if (rc == -1) {
            std::cerr << modbus_strerror(errno);
        } else {
            std::cerr << "unerwartete Anzahl gelesener Register (" << rc << ')';
        }
        std::cerr << '\n';
        return false;
    }

    double final_value = 0.0;

    if (metric.format == RegisterFormat::unsigned16) {
        uint16_t val = tab_reg[0];
        // Huawei Fehlerwert-Erkennung für 16-bit (0x7FFF)
        if (val == 0x7FFF) return false; 
        final_value = static_cast<double>(val) / metric.scale;
    } 
    else if (metric.format == RegisterFormat::signed16) {
        int16_t val = static_cast<int16_t>(tab_reg[0]);
        if (val == 0x7FFF || val == -32768) return false;
        final_value = static_cast<double>(val) / metric.scale;
    } 
    else if (metric.format == RegisterFormat::unsigned32) {
        uint32_t val = (static_cast<uint32_t>(tab_reg[0]) << 16) | tab_reg[1];
        // Huawei Fehlerwert-Erkennung für 32-bit (0x7FFFFFFF)
        if (val == 0x7FFFFFFF) return false;
        final_value = static_cast<double>(val) / metric.scale;
    } 
    else if (metric.format == RegisterFormat::signed32) {
        int32_t val = static_cast<int32_t>(
            (static_cast<uint32_t>(tab_reg[0]) << 16) | tab_reg[1]);
        if (val == 0x7FFFFFFF || val == -2147483648LL) return false;
        final_value = static_cast<double>(val) / metric.scale;
    }

    const std::string value = std::to_string(final_value);
    write_row(log, timestamp, metric.name, value, metric.unit);
    print_value(metric.name, value, metric.unit);
    return true;
}

void handle_signal(int) {
    keep_running = 0;
}

void print_usage(std::ostream& output) {
     output << "Verwendung:\n"
              << "  sun2000Tool poll --host HOST --interval-seconds N [--port PORT] "
              "[--unit-id ID] [--log-file DATEI] [--once]\n"
              << "  sun2000Tool scan --cidr IPV4/PRAEFIX [--port PORT] [--workers N] "
              "[--timeout-ms N] [--max-hosts N]\n"
              << "  sun2000Tool --help\n";
}

unsigned long parse_number(const std::string& value, const std::string& option,
                           unsigned long maximum) {
    std::size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument("Ungueltiger Wert fuer " + option + ": " +
                                    value);
    }
    if (consumed != value.size() || parsed > maximum) {
        throw std::invalid_argument("Wert fuer " + option + " ausserhalb des "
                                    "gueltigen Bereichs: " + value);
    }
    return parsed;
}

std::string take_value(int& index, int argc, char* argv[],
                       const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("Wert fuer " + option + " fehlt.");
    }
    ++index;
    return argv[index];
}

PollOptions parse_poll_options(int argc, char* argv[]) {
    PollOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--host") {
            options.host = take_value(index, argc, argv, argument);
        } else if (argument == "--port") {
            options.port = static_cast<int>(parse_number(
                take_value(index, argc, argv, argument), argument, 65535));
        } else if (argument == "--unit-id") {
            options.unit_id = static_cast<int>(parse_number(
                take_value(index, argc, argv, argument), argument, 247));
        } else if (argument == "--interval-seconds") {
            options.interval_seconds = static_cast<unsigned int>(parse_number(
                take_value(index, argc, argv, argument), argument, 86400));
        } else if (argument == "--log-file") {
            options.log_file = take_value(index, argc, argv, argument);
        } else if (argument == "--once") {
            options.once = true;
        } else if (argument == "--help") {
            print_usage(std::cout);
            std::exit(0);
        } else {
            throw std::invalid_argument("Unbekannte Option: " + argument);
        }
    }

    if (options.host.empty()) {
        throw std::invalid_argument("--host ist erforderlich.");
    }
    if (options.interval_seconds == 0 && !options.once) {
        throw std::invalid_argument(
            "--interval-seconds muss groesser als 0 sein.");
    }
    if (options.port == 0 || options.log_file.empty()) {
        throw std::invalid_argument("Port und Logdatei muessen gueltig sein.");
    }
    return options;
}

modbus::ScanOptions parse_scan_options(int argc, char* argv[],
                                       std::string& cidr) {
    modbus::ScanOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--cidr") {
            cidr = take_value(index, argc, argv, argument);
        } else if (argument == "--port") {
            options.ports = {static_cast<int>(parse_number(
                take_value(index, argc, argv, argument), argument, 65535))};
        } else if (argument == "--workers") {
            options.workers = static_cast<unsigned int>(parse_number(
                take_value(index, argc, argv, argument), argument, 128));
        } else if (argument == "--timeout-ms") {
            options.timeout_ms = static_cast<unsigned int>(parse_number(
                take_value(index, argc, argv, argument), argument, 10000));
        } else if (argument == "--max-hosts") {
            options.max_hosts = parse_number(
                take_value(index, argc, argv, argument), argument, 65536);
        } else if (argument == "--help") {
            print_usage(std::cout);
            std::exit(0);
        } else {
            throw std::invalid_argument("Unbekannte Option: " + argument);
        }
    }
    if (cidr.empty()) {
        throw std::invalid_argument("--cidr ist erforderlich.");
    }
    if (options.ports.front() == 0 || options.workers == 0 ||
        options.timeout_ms == 0 || options.max_hosts == 0) {
        throw std::invalid_argument("Scan-Optionen muessen groesser als 0 sein.");
    }
    return options;
}

std::string timestamp_utc() {
    const std::time_t current_time = std::time(nullptr);
    std::tm utc_time{};
    gmtime_r(&current_time, &utc_time);
    std::ostringstream timestamp;
    timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return timestamp.str();
}

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

void write_row(std::ofstream& log, const std::string& timestamp,
               const std::string& name, const std::string& value,
               const std::string& unit) {
    const std::string row = timestamp + ',' + csv_escape(name) + ',' +
                            csv_escape(value) + ',' + csv_escape(unit);
    log << row << '\n';
}

std::string read_string(modbus_t* context, int address, int register_count) {
    std::vector<std::uint16_t> registers(static_cast<std::size_t>(register_count));
    
    usleep(40000);
    const int result =
        modbus_read_registers(context, address, register_count, registers.data());
    
    if (result != register_count) {
        std::cerr << "KRITISCH: Register " << address
                  << " fehlgeschlagen mit Fehler: "
                  << (result == -1 ? modbus_strerror(errno)
                                   : "unerwartete Anzahl gelesener Register")
                  << " (Ergebnis: " << result << ")\n";
        return {};
    }

    std::string value;
    value.reserve(static_cast<std::size_t>(register_count) * 2);
    for (const auto word : registers) {
        value.push_back(static_cast<char>(word >> 8));
        value.push_back(static_cast<char>(word & 0xFF));
    }
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

// Erweiterte Poll-Funktion, die die vorherige Funktionalität beibehält, aber die Metriken in einer Schleife liest und protokolliert.
bool poll_once_ext(const PollOptions& options, std::ofstream& log) {
    modbus_t* context = modbus_new_tcp(options.host.c_str(), options.port);
    if (context == nullptr) {
        std::cerr << "Modbus-Kontext konnte nicht erstellt werden.\n";
        return false;
    }

    modbus_set_response_timeout(context, 1, 500000);
    modbus_set_slave(context, options.unit_id);
    if (modbus_connect(context) == -1) {
        std::cerr << "Verbindung zu " << options.host << ':' << options.port
                  << " fehlgeschlagen: " << modbus_strerror(errno) << '\n';
        modbus_free(context);
        return false;
    }

    const std::string timestamp = timestamp_utc();
    std::cout << "SUN2000-Messwerte (" << timestamp << " UTC)\n";
    
    // Identifikationsdaten (Holding Registers)
    print_section("Identifikation");
    const std::string model = read_string(context, 30000, 15);
    const std::string serial = read_string(context, 30015, 10);
    const std::string firmware = read_string(context, 30035, 15);
    
    if (!model.empty()) {
        write_row(log, timestamp, "model_name", model, "");
        print_value("Modell", model, "");
    }
    if (!serial.empty()) {
        write_row(log, timestamp, "serial_number", serial, "");
        print_value("Seriennummer", serial, "");
    }
    if (!firmware.empty()) {
        write_row(log, timestamp, "firmware_version", firmware, "");
        print_value("Firmware", firmware, "");
    }

    std::uint16_t model_id = 0;
    const int model_id_result =
        modbus_read_registers(context, 30070, 1, &model_id);
    if (model_id_result == 1) {
        const std::string value = std::to_string(model_id);
        write_row(log, timestamp, "model_id", value, "");
        print_value("Modell-ID", value, "");
    } else {
        std::cerr << "Modell-ID (Register 30070) nicht lesbar: "
                  << (model_id_result == -1
                          ? modbus_strerror(errno)
                          : "unerwartete Anzahl gelesener Register")
                  << '\n';
    }

    // PV-String Anzahl
    print_section("PV-Eingaenge");
    uint16_t pv_string_count = 0;
    uint16_t count_reg = 0;
    const int count_result =
        modbus_read_registers(context, 30071, 1, &count_reg);
    if (count_result == 1) {
        pv_string_count = count_reg;
        const auto bounded_count = std::min<std::uint16_t>(pv_string_count, 24);
        for (std::uint16_t index = 0; index < bounded_count; ++index) {
            const int voltage_address = 32016 + static_cast<int>(index) * 2;
            const std::string suffix = std::to_string(index + 1);
            
            // PV Spannung & Strom sicher abfragen
            Metric v_metric{"pv_" + suffix + "_voltage", "V", voltage_address, RegisterFormat::signed16, 10};
            Metric c_metric{"pv_" + suffix + "_current", "A", voltage_address + 1, RegisterFormat::signed16, 100};
            
            read_and_log_metric(log, timestamp, context, v_metric);
            read_and_log_metric(log, timestamp, context, c_metric);
        }
    } else {
        std::cerr << "PV-String-Anzahl (Register 30071) nicht lesbar: "
                  << (count_result == -1 ? modbus_strerror(errno)
                                         : "unerwartete Anzahl gelesener Register")
                  << '\n';
    }

    // Inverter Metriken
    const std::array<Metric, 23> inverter_metrics{{
        {"input_power", "W", 32064, RegisterFormat::signed32, 1},
        {"line_voltage_a_b", "V", 32066, RegisterFormat::unsigned16, 10},
        {"line_voltage_b_c", "V", 32067, RegisterFormat::unsigned16, 10},
        {"line_voltage_c_a", "V", 32068, RegisterFormat::unsigned16, 10},
        {"phase_voltage_a", "V", 32069, RegisterFormat::unsigned16, 10},
        {"phase_voltage_b", "V", 32070, RegisterFormat::unsigned16, 10},
        {"phase_voltage_c", "V", 32071, RegisterFormat::unsigned16, 10},
        {"phase_current_a", "A", 32072, RegisterFormat::signed32, 1000},
        {"phase_current_b", "A", 32074, RegisterFormat::signed32, 1000},
        {"phase_current_c", "A", 32076, RegisterFormat::signed32, 1000},
        {"active_power", "W", 32080, RegisterFormat::signed32, 1},
        {"reactive_power", "var", 32082, RegisterFormat::signed32, 1},
        {"power_factor", "", 32084, RegisterFormat::signed16, 1000},
        {"grid_frequency", "Hz", 32085, RegisterFormat::unsigned16, 100},
        {"efficiency", "%", 32086, RegisterFormat::unsigned16, 100},
        {"internal_temperature", "C", 32087, RegisterFormat::signed16, 10},
        {"insulation_resistance", "MOhm", 32088, RegisterFormat::unsigned16, 1000},
        {"device_status", "code", 32089, RegisterFormat::unsigned16, 1},
        {"fault_code", "code", 32090, RegisterFormat::unsigned16, 1},
        {"active_power_fast", "W", 32095, RegisterFormat::signed32, 1},
        {"accumulated_yield_energy", "kWh", 32106, RegisterFormat::unsigned32, 100},
        {"daily_yield_energy", "kWh", 32114, RegisterFormat::unsigned32, 100},
        {"monthly_yield_energy", "kWh", 32116, RegisterFormat::unsigned32, 100},
    }};
    
    print_section("Wechselrichter und Netz");
    for (const auto& metric : inverter_metrics) {
        read_and_log_metric(log, timestamp, context, metric);
    }

    // Batteriedaten
    const std::array<Metric, 9> battery_metrics{{
        {"battery_unit_1_status", "code", 37000, RegisterFormat::unsigned16, 1},
        {"battery_unit_1_power", "W", 37001, RegisterFormat::signed32, 1},
        {"battery_unit_1_bus_voltage", "V", 37003, RegisterFormat::unsigned16, 10},
        {"battery_unit_1_soc", "%", 37004, RegisterFormat::unsigned16, 10},
        {"battery_unit_1_working_mode", "code", 37006, RegisterFormat::unsigned16, 1},
        {"battery_unit_1_temperature", "C", 37022, RegisterFormat::signed16, 10},
        {"battery_soc", "%", 37760, RegisterFormat::unsigned16, 10},
        {"battery_power", "W", 37765, RegisterFormat::signed32, 1},
        {"battery_total_charge", "kWh", 37780, RegisterFormat::unsigned32, 100},
    }};
    
    print_section("Batterie");
    for (const auto& metric : battery_metrics) {
        read_and_log_metric(log, timestamp, context, metric);
    }

    log.flush();
    std::cout << "\nAbfrage abgeschlossen.\n";
    modbus_close(context);
    modbus_free(context);
    return static_cast<bool>(log);
}



int run_poll(const PollOptions& options) {
    std::ofstream log(options.log_file, std::ios::app);
    if (!log) {
        throw std::runtime_error("Logdatei kann nicht geoeffnet werden: " +
                                 options.log_file);
    }
    if (std::filesystem::file_size(options.log_file) == 0) {
        log << "timestamp_utc,signal,value,unit\n";
    }

    std::signal(SIGINT, handle_signal);
    do {
        //if (!poll_once(options, log) && options.once) {
        if (!poll_once_ext(options, log) && options.once) {
            return 1;
        }
        if (options.once) {
            break;
        }
        for (unsigned int elapsed = 0;
             elapsed < options.interval_seconds && keep_running; ++elapsed) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } while (keep_running);
    return 0;
}

int run_scan(int argc, char* argv[]) {
    std::string cidr;
    const auto options = parse_scan_options(argc, argv, cidr);
    const auto results = modbus::scan_ipv4_cidr(cidr, options);
    if (results.empty()) {
        std::cout << "Keine offenen Modbus-Ports im Bereich " << cidr << ".\n";
        return 0;
    }

    for (const auto& result : results) {
        std::cout << result.host << ':' << result.port << " - ";
        if (!result.modbus_confirmed) {
            std::cout << "TCP offen, Modbus unbestaetigt\n";
            continue;
        }
        std::cout << "Modbus bestaetigt, Unit-ID(s): ";
        for (std::size_t index = 0;
             index < result.responding_unit_ids.size(); ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            std::cout << result.responding_unit_ids[index];
        }
        std::cout << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 1 || std::string(argv[1]) == "--help" ||
            std::string(argv[1]) == "-h") {
            print_usage(std::cout);
            return 0;
        }

        const std::string mode = argv[1];
        if (mode == "poll") {
            return run_poll(parse_poll_options(argc, argv));            
        }
        if (mode == "scan") {
            return run_scan(argc, argv);
        }
        throw std::invalid_argument("Unbekannter Modus: " + mode);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        print_usage(std::cerr);
        return 2;
    }
}
