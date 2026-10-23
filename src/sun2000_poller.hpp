#pragma once

#include "output.hpp"

#include <string>

namespace sun2000 {

struct PollConnection {
    std::string host;
    int port{502};
    int unit_id{1};
};

class Sun2000Poller {
public:
    Sun2000Poller(PollConnection connection, CsvWriter* csv,
                  ConsoleReporter& console);

    bool poll_once();

private:
    PollConnection connection_;
    CsvWriter* csv_;
    ConsoleReporter& console_;
};

}  // namespace sun2000
