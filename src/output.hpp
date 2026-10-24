#pragma once

#include <fstream>
#include <iostream>
#include <string>

namespace sun2000 {

enum class ConsoleOutputMode {
    summary,
    all
};

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);

    void write(const std::string& timestamp, const std::string& signal,
               const std::string& value, const std::string& unit);
    void flush();

private:
    void open_hour(const std::string& hour_key);

    std::string base_path_;
    std::string active_hour_;
    std::ofstream stream_;
};

class ConsoleReporter {
public:
    explicit ConsoleReporter(
        std::ostream& output,
        ConsoleOutputMode mode = ConsoleOutputMode::summary,
        std::ostream& diagnostics = std::cerr);

    void begin_poll(const std::string& timestamp);
    void section(const std::string& title);
    void value(const std::string& signal, const std::string& label,
               const std::string& value, const std::string& unit = "");
    void diagnostic(const std::string& message);
    void finish_poll();

private:
    std::ostream& output_;
    std::ostream& diagnostics_;
    ConsoleOutputMode mode_;
};

}  // namespace sun2000
