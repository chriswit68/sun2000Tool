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

void test_reads_complete_csv_hour() {
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

    const auto records = sun2000::TimescaleWriter::read_csv_records(
        csv_path.string());
    expect(records.size() == 5,
           "Alle Messwerte aus der CSV-Stunde muessen erhalten bleiben.");
    expect(records[0].timestamp == "2026-10-24T05:59:00Z" &&
               records[0].value == "400.000000",
           "Erster Messsatz der CSV-Stunde fehlt.");
    expect(records[1].timestamp == "2026-10-24T05:59:00Z" &&
               records[1].signal == "model_name" &&
               records[1].value == "old",
           "Frueher Textmesswert wurde ausgelassen.");
    expect(records[2].timestamp == "2026-10-24T05:59:30Z" &&
               records[2].value == "500.000000",
           "Frueher Pollwert innerhalb der Stunde wurde ausgelassen.");
    expect(records[3].value == "SUN2000, \"Hybrid\" inverter",
           "CSV-Quotes oder Kommas wurden nicht korrekt gelesen.");
    expect(records[4].value == "520.000000",
           "Letzter Pollwert innerhalb der Stunde fehlt.");
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
            sun2000::TimescaleWriter::read_csv_records(csv_path.string()));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::filesystem::remove(csv_path);
    expect(rejected, "Ungueltiger CSV-Header wurde akzeptiert.");
}

}  // namespace

int main() {
    test_reads_complete_csv_hour();
    test_rejects_bad_csv_header();
}
