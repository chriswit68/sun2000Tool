#include "output.hpp"

#include <filesystem>
#include <iomanip>
#include <stdexcept>

namespace sun2000 {
namespace {

std::string csv_escape(const std::string& value) {
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

}  // namespace

CsvWriter::CsvWriter(const std::string& path)
    : stream_(path, std::ios::app) {
    if (!stream_) {
        throw std::runtime_error("Logdatei kann nicht geoeffnet werden: " +
                                 path);
    }
    if (!std::filesystem::exists(path) ||
        std::filesystem::file_size(path) == 0) {
        stream_ << "timestamp_utc,signal,value,unit\n";
        if (!stream_) {
            throw std::runtime_error("CSV-Kopfzeile konnte nicht geschrieben werden: " +
                                     path);
        }
    }
}

void CsvWriter::write(const std::string& timestamp, const std::string& signal,
                      const std::string& value, const std::string& unit) {
    stream_ << timestamp << ',' << csv_escape(signal) << ','
            << csv_escape(value) << ',' << csv_escape(unit) << '\n';
    if (!stream_) {
        throw std::runtime_error("CSV-Datensatz konnte nicht geschrieben werden.");
    }
}

void CsvWriter::flush() {
    stream_.flush();
    if (!stream_) {
        throw std::runtime_error("CSV-Datei konnte nicht geschrieben werden.");
    }
}

ConsoleReporter::ConsoleReporter(std::ostream& output) : output_(output) {}

void ConsoleReporter::begin_poll(const std::string& timestamp) {
    output_ << "SUN2000-Messwerte (" << timestamp << " UTC)\n";
}

void ConsoleReporter::section(const std::string& title) {
    output_ << '\n' << title << '\n';
}

void ConsoleReporter::value(const std::string& label, const std::string& value,
                            const std::string& unit) {
    output_ << "  " << std::left << std::setw(28) << label << value;
    if (!unit.empty()) {
        output_ << ' ' << unit;
    }
    output_ << '\n';
}

void ConsoleReporter::finish_poll() {
    output_ << "\nAbfrage abgeschlossen.\n";
}

}  // namespace sun2000
