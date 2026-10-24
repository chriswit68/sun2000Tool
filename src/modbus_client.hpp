#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct _modbus;
using modbus_t = _modbus;

namespace sun2000 {

struct ModbusReadStats {
    std::size_t attempts{};
    std::size_t succeeded{};
    std::size_t failed{};
};

// Kapselt eine verbundene Modbus-TCP-Verbindung. Das Objekt besitzt den
// libmodbus-Kontext; Kopien sind deshalb nicht erlaubt.
class ModbusClient {
public:
    // Baut die Verbindung auf. Wirft std::runtime_error bei Konfigurations-
    // oder Verbindungsfehlern.
    ModbusClient(const std::string& host, int port, int unit_id);
    ~ModbusClient();

    ModbusClient(const ModbusClient&) = delete;
    ModbusClient& operator=(const ModbusClient&) = delete;

    // Liest Holding-Register (Modbus-Funktion 03); wirft bei Lesefehlern.
    std::vector<std::uint16_t> read_holding_registers(int address,
                                                      int count) const;

    // Liest Register als Zeichenfolge, wobei jedes Register zuerst sein
    // hohes, dann sein niedriges Byte beitraegt.
    std::string read_string(int address, int register_count) const;

    // Fallback fuer Geraete, die einen Block-Stringzugriff ablehnen.
    // Wirft, wenn mindestens eines der Einzelregister nicht gelesen werden kann.
    std::string read_string_register_by_register(
        int address, int register_count) const;

    ModbusReadStats read_stats() const;

private:
    modbus_t* context_{};
    mutable ModbusReadStats read_stats_;
};

}  // namespace sun2000
