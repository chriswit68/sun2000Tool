#include "output.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_csv_and_console_outputs_are_separate() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("sun2000-output-test-" +
                       std::to_string(unique_suffix) + ".csv");
    std::ostringstream console_stream;

    {
        sun2000::CsvWriter csv(path.string());
        sun2000::ConsoleReporter console(console_stream);
        csv.write("2026-10-23T08:00:00Z", "model_id", "487", "");
        csv.flush();
        console.begin_poll("2026-10-23T08:00:00Z");
        console.section("Identifikation");
        console.value("Modell-ID", "487");
        console.finish_poll();
    }

    std::ifstream csv_stream(path);
    const std::string csv_contents((std::istreambuf_iterator<char>(csv_stream)),
                                   std::istreambuf_iterator<char>());
    std::filesystem::remove(path);

    expect(csv_contents.find("timestamp_utc,signal,value,unit\n") == 0,
           "CSV-Kopfzeile fehlt.");
    expect(csv_contents.find("\"model_id\",\"487\"") != std::string::npos,
           "CSV-Messwert fehlt.");
    expect(console_stream.str().find("Modell-ID") != std::string::npos,
           "Konsolenwert fehlt.");
    expect(console_stream.str().find("\"model_id\"") == std::string::npos,
           "Konsolenausgabe enthaelt CSV-Datensatz.");
}

}  // namespace

int main() {
    test_csv_and_console_outputs_are_separate();
}
