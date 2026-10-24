#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sun2000 {

enum class RegisterFormat {
    unsigned16,
    signed16,
    unsigned32,
    signed32
};

inline std::optional<double> decode_register_value(
    const std::vector<std::uint16_t>& registers, RegisterFormat format,
    double scale) {
    if (!std::isfinite(scale) || scale <= 0) {
        throw std::invalid_argument("Register-Skalierung muss groesser 0 sein.");
    }

    const bool is_32_bit = format == RegisterFormat::unsigned32 ||
                           format == RegisterFormat::signed32;
    const std::size_t expected_count = is_32_bit ? 2 : 1;
    if (registers.size() != expected_count) {
        throw std::invalid_argument("Unerwartete Anzahl Modbus-Register.");
    }

    std::int64_t decoded = 0;
    switch (format) {
        case RegisterFormat::unsigned16:
            if (registers[0] == 0x7FFF) {
                return std::nullopt;
            }
            decoded = registers[0];
            break;
        case RegisterFormat::signed16:
            if (registers[0] == 0x7FFF || registers[0] == 0x8000) {
                return std::nullopt;
            }
            decoded = registers[0] <= 0x7FFF
                          ? registers[0]
                          : static_cast<std::int32_t>(registers[0]) - 0x10000;
            break;
        case RegisterFormat::unsigned32: {
            const std::uint32_t raw =
                (static_cast<std::uint32_t>(registers[0]) << 16) | registers[1];
            if (raw == 0x7FFFFFFF) {
                return std::nullopt;
            }
            decoded = raw;
            break;
        }
        case RegisterFormat::signed32: {
            const std::uint32_t raw =
                (static_cast<std::uint32_t>(registers[0]) << 16) | registers[1];
            if (raw == 0x7FFFFFFF || raw == 0x80000000) {
                return std::nullopt;
            }
            decoded = raw <= 0x7FFFFFFF
                          ? static_cast<std::int64_t>(raw)
                          : static_cast<std::int64_t>(raw) - 0x100000000LL;
            break;
        }
        default:
            throw std::invalid_argument("Unbekanntes Modbus-Registerformat.");
    }
    return static_cast<double>(decoded) / scale;
}

}  // namespace sun2000
