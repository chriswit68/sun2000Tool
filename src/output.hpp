#pragma once

#include <fstream>
#include <iosfwd>
#include <string>

namespace sun2000 {

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);

    void write(const std::string& timestamp, const std::string& signal,
               const std::string& value, const std::string& unit);
    void flush();

private:
    std::ofstream stream_;
};

class ConsoleReporter {
public:
    explicit ConsoleReporter(std::ostream& output);

    void begin_poll(const std::string& timestamp);
    void section(const std::string& title);
    void value(const std::string& label, const std::string& value,
               const std::string& unit = "");
    void finish_poll();

private:
    std::ostream& output_;
};

}  // namespace sun2000
