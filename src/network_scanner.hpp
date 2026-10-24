#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace modbus {

// Grenzen und Parallelisierung fuer einen Netzwerkscan. workers ist auf 1..128,
// timeout_ms auf 1..10000 begrenzt.
struct ScanOptions {
    std::vector<int> ports{502, 6607};
    std::vector<int> unit_ids{0, 1};
    unsigned int workers{16};
    unsigned int timeout_ms{350};
    std::uint64_t max_hosts{4096};
};

// Ein TCP-Port kann offen sein, ohne dass ein Modbus-Geraet antwortet.
// modbus_confirmed und responding_unit_ids zeigen eine Protokollantwort an.
struct ScanResult {
    std::string host;
    int port{};
    bool port_open{};
    bool modbus_confirmed{};
    std::vector<int> responding_unit_ids;
};

// Erzeugt die IPv4-Hostliste des CIDR-Bereichs. Netzwerk- und Broadcastadresse
// werden bei Praefixen bis /30 ausgelassen. Wirft bei ungueltigem/zu grossem CIDR.
std::vector<std::string> enumerate_ipv4_cidr(const std::string& cidr,
                                             std::uint64_t max_hosts);

// Prueft konfigurierte Ports und Unit-IDs parallel; nur offene Ports erscheinen
// im Ergebnis. Wirft std::invalid_argument bei ungueltigen Scan-Optionen.
std::vector<ScanResult> scan_ipv4_cidr(const std::string& cidr,
                                       const ScanOptions& options);

}  // namespace modbus