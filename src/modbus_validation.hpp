#pragma once

#include <stdexcept>
#include <string>

namespace sun2000 {
namespace detail {

inline void validate_connection_parameters(const std::string& host, int port,
                                          int unit_id) {
    if (host.empty()) {
        throw std::invalid_argument("Modbus-Host darf nicht leer sein.");
    }
    if (port < 1 || port > 65535) {
        throw std::invalid_argument(
            "Modbus-Port muss zwischen 1 und 65535 liegen.");
    }
    if (unit_id < 0 || unit_id > 247) {
        throw std::invalid_argument(
            "Modbus-Unit-ID muss zwischen 0 und 247 liegen.");
    }
}

inline void validate_register_read(int address, int count) {
    constexpr int maximum_read_count = 125;
    constexpr int maximum_address = 65535;
    if (address < 0 || address > maximum_address || count < 1 ||
        count > maximum_read_count ||
        address > maximum_address - (count - 1)) {
        throw std::invalid_argument(
            "Modbus-Registeradresse oder Anzahl ausserhalb des gueltigen Bereichs.");
    }
}

}  // namespace detail
}  // namespace sun2000
