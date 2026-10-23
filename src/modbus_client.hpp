#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct _modbus;
using modbus_t = _modbus;

namespace sun2000 {

class ModbusClient {
public:
    ModbusClient(const std::string& host, int port, int unit_id);
    ~ModbusClient();

    ModbusClient(const ModbusClient&) = delete;
    ModbusClient& operator=(const ModbusClient&) = delete;

    std::vector<std::uint16_t> read_holding_registers(int address,
                                                      int count) const;
    std::string read_string(int address, int register_count) const;
    std::string read_string_register_by_register(
        int address, int register_count) const;

private:
    modbus_t* context_{};
};

}  // namespace sun2000
