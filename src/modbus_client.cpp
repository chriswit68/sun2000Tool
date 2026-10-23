#include "modbus_client.hpp"

#include <cerrno>
#include <stdexcept>
#include <vector>

#include <modbus/modbus.h>

namespace sun2000 {

ModbusClient::ModbusClient(const std::string& host, int port, int unit_id) {
    context_ = modbus_new_tcp(host.c_str(), port);
    if (context_ == nullptr) {
        throw std::runtime_error("Modbus-Kontext konnte nicht erstellt werden.");
    }

    if (modbus_set_response_timeout(context_, 1, 500000) == -1 ||
        modbus_set_slave(context_, unit_id) == -1) {
        const std::string error = modbus_strerror(errno);
        modbus_free(context_);
        context_ = nullptr;
        throw std::runtime_error("Modbus-Konfiguration fehlgeschlagen: " +
                                 error);
    }
    if (modbus_connect(context_) == -1) {
        const std::string error = modbus_strerror(errno);
        modbus_free(context_);
        context_ = nullptr;
        throw std::runtime_error("Verbindung zu " + host + ':' +
                                 std::to_string(port) +
                                 " fehlgeschlagen: " + error);
    }
}

ModbusClient::~ModbusClient() {
    if (context_ != nullptr) {
        modbus_close(context_);
        modbus_free(context_);
    }
}

std::vector<std::uint16_t> ModbusClient::read_holding_registers(
    int address, int count) const {
    std::vector<std::uint16_t> registers(static_cast<std::size_t>(count));
    const int result =
        modbus_read_registers(context_, address, count, registers.data());
    if (result != count) {
        const std::string reason =
            result == -1 ? modbus_strerror(errno)
                         : "unerwartete Anzahl gelesener Register";
        throw std::runtime_error("Register " + std::to_string(address) +
                                 " nicht lesbar: " + reason);
    }
    return registers;
}

std::string ModbusClient::read_string(int address, int register_count) const {
    const auto registers = read_holding_registers(address, register_count);
    std::string value;
    value.reserve(registers.size() * 2);
    for (const auto word : registers) {
        value.push_back(static_cast<char>(word >> 8));
        value.push_back(static_cast<char>(word & 0xFF));
    }
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

}  // namespace sun2000
