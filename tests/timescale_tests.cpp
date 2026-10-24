#include "timescale_writer.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_latest_csv_sample() {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("sun2000-timescale-test-" +
                            std::to_string(static_cast<unsigned long long>(
                                std::filesystem::file_time_type::clock::now()
                                    .time_since_epoch()
                                    .count())));
    std::filesystem::create_directories(directory);
    const auto csv_path = directory / "hour.csv";
    {
        std::ofstream csv(csv_path);
        csv << "timestamp_utc,signal,value,unit\n"
            << "2026-10-24T05:59:00Z,\"active_power\",\"400.000000\",\"W\"\n"
            << "2026-10-24T05:59:00Z,\"model_name\",\"old\",\"\"\n"
            << "2026-10-24T05:59:30Z,\"active_power\",\"500.000000\",\"W\"\n"
            << "2026-10-24T05:59:30Z,\"model_name\","
               "\"SUN2000, \"\"Hybrid\"\" inverter\",\"\"\n"
            << "2026-10-24T05:59:30Z,\"active_power\",\"520.000000\",\"W\"\n";
    }

    const auto sample = sun2000::TimescaleWriter::read_latest_sample(
        csv_path.string());
    expect(sample.size() == 2,
           "Nur Signale des neuesten CSV-Zeitstempels werden gespeichert.");
    bool found_power = false;
    bool found_model = false;
    for (const auto& record : sample) {
        expect(record.timestamp == "2026-10-24T05:59:30Z",
               "Messwert hat nicht den neuesten CSV-Zeitstempel.");
        if (record.signal == "active_power") {
            found_power = record.value == "520.000000" && record.unit == "W";
        } else if (record.signal == "model_name") {
            found_model = record.value == "SUN2000, \"Hybrid\" inverter";
        }
    }
    expect(found_power, "Letzter gleichzeitiger Leistungswert fehlt.");
    expect(found_model, "CSV-Quotes oder Kommas wurden nicht korrekt gelesen.");
    std::filesystem::remove_all(directory);
}

void test_rejects_bad_csv_header() {
    const auto csv_path = std::filesystem::temp_directory_path() /
                          ("sun2000-timescale-bad-" +
                           std::to_string(static_cast<unsigned long long>(
                               std::filesystem::file_time_type::clock::now()
                                   .time_since_epoch()
                                   .count())) +
                           ".csv");
    {
        std::ofstream csv(csv_path);
        csv << "timestamp,signal,value,unit\n";
    }
    bool rejected = false;
    try {
        static_cast<void>(
            sun2000::TimescaleWriter::read_latest_sample(csv_path.string()));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::filesystem::remove(csv_path);
    expect(rejected, "Ungueltiger CSV-Header wurde akzeptiert.");
}

}  // namespace

int main() {
    test_latest_csv_sample();
    test_rejects_bad_csv_header();
}
