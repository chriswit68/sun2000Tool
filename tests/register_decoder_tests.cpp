#include "modbus_validation.hpp"
#include "register_decoder.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_equal(double actual, double expected, const std::string& label) {
    if (actual != expected) {
        throw std::runtime_error(label + ": unerwarteter dekodierter Wert.");
    }
}

void expect_invalid(const std::vector<std::uint16_t>& registers,
                    sun2000::RegisterFormat format, double scale) {
    try {
        static_cast<void>(
            sun2000::decode_register_value(registers, format, scale));
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Ungueltige Registerdaten wurden akzeptiert.");
}

template <typename Function>
void expect_throws(Function&& function, const std::string& label) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(label + " wurde nicht abgelehnt.");
}

void test_register_decoding() {
    using sun2000::RegisterFormat;
    const auto unsigned16 =
        sun2000::decode_register_value({0x1234}, RegisterFormat::unsigned16, 1);
    expect_equal(*unsigned16, 4660, "unsigned16");

    const auto signed16 =
        sun2000::decode_register_value({0xFF9C}, RegisterFormat::signed16, 10);
    expect_equal(*signed16, -10, "signed16");

    const auto unsigned32 = sun2000::decode_register_value(
        {0x0001, 0x86A0}, RegisterFormat::unsigned32, 100);
    expect_equal(*unsigned32, 1000, "unsigned32");

    const auto signed32 = sun2000::decode_register_value(
        {0xFFFF, 0xFF9C}, RegisterFormat::signed32, 1);
    expect_equal(*signed32, -100, "signed32");

    if (sun2000::decode_register_value({0x7FFF}, RegisterFormat::unsigned16, 1) ||
        sun2000::decode_register_value({0x8000}, RegisterFormat::signed16, 1) ||
        sun2000::decode_register_value({0x7FFF, 0xFFFF},
                                       RegisterFormat::unsigned32, 1) ||
        sun2000::decode_register_value({0x8000, 0x0000},
                                       RegisterFormat::signed32, 1)) {
        throw std::runtime_error("Sentinelwert wurde als Messwert dekodiert.");
    }

    expect_invalid({}, RegisterFormat::unsigned16, 1);
    expect_invalid({0, 0}, RegisterFormat::signed16, 1);
    expect_invalid({0}, RegisterFormat::unsigned32, 1);
    expect_invalid({0, 0}, RegisterFormat::signed32, 0);
    expect_invalid({1}, RegisterFormat::unsigned16,
                   std::numeric_limits<double>::infinity());
    expect_invalid({1}, RegisterFormat::unsigned16,
                   std::numeric_limits<double>::quiet_NaN());
    expect_invalid({1}, static_cast<RegisterFormat>(99), 1);
}

void test_modbus_parameter_validation() {
    sun2000::detail::validate_connection_parameters("inverter", 502, 1);
    sun2000::detail::validate_register_read(0, 1);
    sun2000::detail::validate_register_read(65411, 125);
    sun2000::detail::validate_register_read(65535, 1);

    expect_throws(
        [] { sun2000::detail::validate_connection_parameters("", 502, 1); },
        "Leerer Host");
    expect_throws(
        [] { sun2000::detail::validate_connection_parameters("host", 0, 1); },
        "Port 0");
    expect_throws(
        [] { sun2000::detail::validate_connection_parameters("host", 502, 248); },
        "Unit-ID 248");
    expect_throws([] { sun2000::detail::validate_register_read(-1, 1); },
                  "Negative Adresse");
    expect_throws([] { sun2000::detail::validate_register_read(0, 0); },
                  "Leere Leseanzahl");
    expect_throws([] { sun2000::detail::validate_register_read(0, 126); },
                  "Zu viele Register");
    expect_throws([] { sun2000::detail::validate_register_read(65535, 2); },
                  "Adressbereich-Ueberlauf");
}

}  // namespace

int main() {
    try {
        test_register_decoding();
        test_modbus_parameter_validation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
