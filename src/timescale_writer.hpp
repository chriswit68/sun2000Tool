#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace sun2000 {

struct TimescaleConfig {
    std::string host;
    int port{5444};
    std::string database;
    std::string user;
    std::string password;
};

struct TelemetryRecord {
    std::string timestamp;
    std::string signal;
    std::string value;
    std::string unit;
};

class TimescaleWriter {
public:
    explicit TimescaleWriter(const TimescaleConfig& config);
    ~TimescaleWriter();

    TimescaleWriter(const TimescaleWriter&) = delete;
    TimescaleWriter& operator=(const TimescaleWriter&) = delete;

    std::size_t store_csv_file(const std::string& csv_path);

    static std::vector<TelemetryRecord> read_csv_records(
        const std::string& csv_path);

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace sun2000
