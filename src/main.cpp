#include "network_scanner.hpp"
#include "output.hpp"
#include "sun2000_poller.hpp"

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t keep_running = 1;

struct PollOptions {
    std::string host;
    std::string log_file{"sun2000.csv"};
    int port{502};
    int unit_id{1};
    unsigned int interval_seconds{};
    bool once{};
    sun2000::ConsoleOutputMode console_output{
        sun2000::ConsoleOutputMode::summary};
};

void handle_signal(int) {
    keep_running = 0;
}

void print_usage(std::ostream& output) {
    output << "Verwendung:\n"
           << "  sun2000Tool poll --host HOST --interval-seconds N [--port PORT] "
              "[--unit-id ID] [--log-file DATEI] [--once] "
              "[--console-output summary|all]\n"
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
    return argv[++index];
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
        } else if (argument == "--console-output") {
            const std::string value = take_value(index, argc, argv, argument);
            if (value == "summary") {
                options.console_output = sun2000::ConsoleOutputMode::summary;
            } else if (value == "all") {
                options.console_output = sun2000::ConsoleOutputMode::all;
            } else {
                throw std::invalid_argument(
                    "Ungueltiger Wert fuer --console-output: " + value +
                    " (erlaubt: summary, all)");
            }
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

int run_poll(const PollOptions& options) {
    sun2000::CsvWriter csv(options.log_file);
    sun2000::ConsoleReporter console(std::cout, options.console_output);
    sun2000::Sun2000Poller poller(
        {options.host, options.port, options.unit_id}, csv, console);

    std::signal(SIGINT, handle_signal);
    do {
        if (!poller.poll_once() && options.once) {
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
