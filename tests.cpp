#include "network_scanner.hpp"

#include <iostream>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

void expect_hosts(const std::string& cidr, std::uint64_t max_hosts,
                  const std::vector<std::string>& expected) {
    const auto actual = modbus::enumerate_ipv4_cidr(cidr, max_hosts);
    if (actual != expected) {
        throw std::runtime_error("Unerwartete Hostliste für " + cidr);
    }
}

void expect_invalid(const std::string& cidr, std::uint64_t max_hosts) {
    try {
        static_cast<void>(modbus::enumerate_ipv4_cidr(cidr, max_hosts));
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Ungültiges CIDR wurde akzeptiert: " + cidr);
}

void expect_modbus_scan_confirmation() {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        throw std::runtime_error("Testserver-Socket konnte nicht erstellt werden.");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) < 0 ||
        ::listen(listener, 1) < 0) {
        ::close(listener);
        throw std::runtime_error("Lokaler Modbus-Testserver konnte nicht starten.");
    }

    socklen_t address_length = sizeof(address);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                      &address_length) < 0) {
        ::close(listener);
        throw std::runtime_error("Testserver-Port konnte nicht gelesen werden.");
    }

    std::thread server([listener]() {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client >= 0) {
            std::array<std::uint8_t, 12> request{};
            const auto received = ::recv(client, request.data(), request.size(),
                                         MSG_WAITALL);
            if (received == static_cast<ssize_t>(request.size())) {
                const std::array<std::uint8_t, 9> response{
                    request[0], request[1], 0, 0, 0, 3, request[6],
                    static_cast<std::uint8_t>(request[7] | 0x80), 2};
                static_cast<void>(::send(client, response.data(), response.size(),
                                          0));
            }
            ::close(client);
        }
        ::close(listener);
    });

    modbus::ScanOptions options;
    options.ports = {ntohs(address.sin_port)};
    options.unit_ids = {1};
    options.workers = 1;
    options.timeout_ms = 1000;
    options.max_hosts = 1;
    const auto results = modbus::scan_ipv4_cidr("127.0.0.1/32", options);
    server.join();

    if (results.size() != 1 || !results.front().port_open ||
        !results.front().modbus_confirmed ||
        results.front().responding_unit_ids != std::vector<int>{1}) {
        throw std::runtime_error("Modbus-Testserver wurde nicht bestaetigt.");
    }
}

}  // namespace

int main() {
    try {
        expect_hosts("192.168.1.0/30", 4,
                     {"192.168.1.1", "192.168.1.2"});
        expect_hosts("192.168.1.7/32", 1, {"192.168.1.7"});
        expect_hosts("192.168.1.8/31", 2,
                     {"192.168.1.8", "192.168.1.9"});
        expect_invalid("192.168.1.0/24", 10);
        expect_invalid("not-an-ip/24", 100);
        expect_invalid("192.168.1.0/33", 100);
        expect_modbus_scan_confirmation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}