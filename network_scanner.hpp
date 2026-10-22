#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace modbus {

struct ScanOptions {
    std::vector<int> ports{502, 6607};
    std::vector<int> unit_ids{0, 1};
    unsigned int workers{16};
    unsigned int timeout_ms{350};
    std::uint64_t max_hosts{4096};
};

struct ScanResult {
    std::string host;
    int port{};
    bool port_open{};
    bool modbus_confirmed{};
    std::vector<int> responding_unit_ids;
};

std::vector<std::string> enumerate_ipv4_cidr(const std::string& cidr,
                                             std::uint64_t max_hosts);
std::vector<ScanResult> scan_ipv4_cidr(const std::string& cidr,
                                       const ScanOptions& options);

}  // namespace modbus