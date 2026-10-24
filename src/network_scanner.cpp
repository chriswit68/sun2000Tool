#include "network_scanner.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <modbus/modbus.h>

namespace modbus {
namespace {

int connect_with_timeout(const std::string& host, int port,
                         unsigned int timeout_ms) {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    const int original_flags = ::fcntl(socket_fd, F_GETFL, 0);
    if (original_flags < 0 ||
        ::fcntl(socket_fd, F_SETFL, original_flags | O_NONBLOCK) < 0) {
        ::close(socket_fd);
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(socket_fd);
        return -1;
    }

    int connect_result = ::connect(
        socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (connect_result < 0 && errno == EINPROGRESS) {
        pollfd descriptor{};
        descriptor.fd = socket_fd;
        descriptor.events = POLLOUT;
        connect_result = ::poll(&descriptor, 1, static_cast<int>(timeout_ms));
        if (connect_result > 0) {
            int socket_error = 0;
            socklen_t error_length = sizeof(socket_error);
            if (::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error,
                             &error_length) < 0 || socket_error != 0) {
                connect_result = -1;
            } else {
                connect_result = 0;
            }
        } else {
            connect_result = -1;
        }
    }

    if (connect_result < 0 ||
        ::fcntl(socket_fd, F_SETFL, original_flags) < 0) {
        ::close(socket_fd);
        return -1;
    }
    return socket_fd;
}

bool is_modbus_exception(int error_code) {
    return error_code > MODBUS_ENOBASE &&
           error_code <= MODBUS_ENOBASE + MODBUS_EXCEPTION_MAX;
}

class ModbusProbe {
public:
    ModbusProbe(const std::string& host, int port, int socket_fd,
                unsigned int timeout_ms) {
        context_ = modbus_new_tcp(host.c_str(), port);
        if (context_ == nullptr) {
            ::close(socket_fd);
            return;
        }

        modbus_set_socket(context_, socket_fd);
        const auto timeout_seconds = timeout_ms / 1000;
        const auto timeout_microseconds = (timeout_ms % 1000) * 1000;
        configured_ =
            modbus_set_response_timeout(context_, timeout_seconds,
                                        timeout_microseconds) == 0;
    }

    ~ModbusProbe() {
        if (context_ != nullptr) {
            modbus_close(context_);
            modbus_free(context_);
        }
    }

    ModbusProbe(const ModbusProbe&) = delete;
    ModbusProbe& operator=(const ModbusProbe&) = delete;

    std::vector<int> responding_unit_ids(const std::vector<int>& unit_ids) {
        std::vector<int> responding;
        if (context_ == nullptr || !configured_) {
            return responding;
        }

        for (const int unit_id : unit_ids) {
            if (modbus_set_slave(context_, unit_id) != 0) {
                continue;
            }

            std::uint16_t value = 0;
            const int read_result =
                modbus_read_registers(context_, 0, 1, &value);
            const int read_error = errno;
            if (read_result == 1 || is_modbus_exception(read_error)) {
                responding.push_back(unit_id);
            }
        }
        return responding;
    }

private:
    modbus_t* context_{};
    bool configured_{};
};

ScanResult scan_endpoint(const std::string& host, int port,
                         const ScanOptions& options) {
    ScanResult result;
    result.host = host;
    result.port = port;

    const int socket_fd = connect_with_timeout(host, port, options.timeout_ms);
    if (socket_fd < 0) {
        return result;
    }
    result.port_open = true;

    ModbusProbe probe(host, port, socket_fd, options.timeout_ms);
    result.responding_unit_ids = probe.responding_unit_ids(options.unit_ids);
    result.modbus_confirmed = !result.responding_unit_ids.empty();
    return result;
}

}  // namespace

std::vector<std::string> enumerate_ipv4_cidr(const std::string& cidr,
                                             std::uint64_t max_hosts) {
    const auto separator = cidr.find('/');
    if (separator == std::string::npos || separator == 0 ||
        separator == cidr.size() - 1 ||
        cidr.find('/', separator + 1) != std::string::npos) {
        throw std::invalid_argument("CIDR muss die Form IPv4/Präfix haben.");
    }

    const std::string address_text = cidr.substr(0, separator);
    const std::string prefix_text = cidr.substr(separator + 1);
    std::size_t prefix_chars = 0;
    unsigned long prefix = 0;
    try {
        prefix = std::stoul(prefix_text, &prefix_chars);
    } catch (const std::exception&) {
        throw std::invalid_argument("Ungültiges CIDR-Präfix.");
    }
    if (prefix_chars != prefix_text.size() || prefix > 32) {
        throw std::invalid_argument("CIDR-Präfix muss zwischen 0 und 32 liegen.");
    }

    in_addr parsed_address{};
    if (::inet_pton(AF_INET, address_text.c_str(), &parsed_address) != 1) {
        throw std::invalid_argument("Ungültige IPv4-Adresse im CIDR.");
    }

    const std::uint64_t address_count = std::uint64_t{1} << (32 - prefix);
    const std::uint64_t host_count =
        prefix <= 30 ? address_count - 2 : address_count;
    if (host_count == 0 || host_count > max_hosts) {
        throw std::invalid_argument("CIDR umfasst zu viele Host-Adressen.");
    }

    const std::uint32_t mask = prefix == 0
                                   ? 0
                                   : 0xFFFFFFFFu << (32 - prefix);
    const std::uint32_t network = ntohl(parsed_address.s_addr) & mask;
    const std::uint32_t first_host = prefix <= 30 ? network + 1 : network;

    std::vector<std::string> hosts;
    hosts.reserve(static_cast<std::size_t>(host_count));
    for (std::uint64_t offset = 0; offset < host_count; ++offset) {
        in_addr host_address{};
        host_address.s_addr =
            htonl(first_host + static_cast<std::uint32_t>(offset));
        char host_buffer[INET_ADDRSTRLEN]{};
        if (::inet_ntop(AF_INET, &host_address, host_buffer,
                        sizeof(host_buffer)) == nullptr) {
            throw std::runtime_error("IPv4-Adresse konnte nicht formatiert werden.");
        }
        hosts.emplace_back(host_buffer);
    }
    return hosts;
}

std::vector<ScanResult> scan_ipv4_cidr(const std::string& cidr,
                                       const ScanOptions& options) {
    if (options.ports.empty() || options.unit_ids.empty() ||
        options.workers == 0 || options.workers > 128 ||
        options.timeout_ms == 0 || options.timeout_ms > 10000 ||
        options.max_hosts == 0) {
        throw std::invalid_argument("Ungültige Scan-Optionen.");
    }
    for (const int port : options.ports) {
        if (port < 1 || port > 65535) {
            throw std::invalid_argument("Port muss zwischen 1 und 65535 liegen.");
        }
    }
    for (const int unit_id : options.unit_ids) {
        if (unit_id < 0 || unit_id > 247) {
            throw std::invalid_argument("Unit-ID muss zwischen 0 und 247 liegen.");
        }
    }

    const auto hosts = enumerate_ipv4_cidr(cidr, options.max_hosts);
    std::vector<ScanResult> results;
    std::mutex results_mutex;
    std::atomic<std::size_t> next_host{0};
    std::atomic<bool> stop_workers{false};
    std::exception_ptr worker_error;
    const auto worker_count =
        std::min<std::size_t>(options.workers, hosts.size());
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    const auto join_workers = [&workers]() {
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    };
    try {
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&]() {
                try {
                    while (!stop_workers.load()) {
                        const auto host_index = next_host.fetch_add(1);
                        if (host_index >= hosts.size()) {
                            break;
                        }

                        for (const int port : options.ports) {
                            auto result =
                                scan_endpoint(hosts[host_index], port, options);
                            if (result.port_open) {
                                std::lock_guard<std::mutex> lock(results_mutex);
                                results.push_back(std::move(result));
                            }
                        }
                    }
                } catch (...) {
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);
                        if (!worker_error) {
                            worker_error = std::current_exception();
                        }
                    }
                    stop_workers.store(true);
                }
            });
        }
    } catch (...) {
        stop_workers.store(true);
        join_workers();
        throw;
    }

    join_workers();
    if (worker_error) {
        std::rethrow_exception(worker_error);
    }
    std::sort(results.begin(), results.end(),
              [](const ScanResult& left, const ScanResult& right) {
                  if (left.host != right.host) {
                      return left.host < right.host;
                  }
                  return left.port < right.port;
              });
    return results;
}

}  // namespace modbus