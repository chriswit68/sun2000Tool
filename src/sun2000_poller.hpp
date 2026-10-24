#pragma once

#include "output.hpp"

#include <cstddef>
#include <string>

namespace sun2000 {

struct PollResult {
    bool connected{};
    // Zaehlt Modbus-Leseaufrufe; ein String-Block mit mehreren Registern ist
    // ein Aufruf, Einzelregister-Fallbacks sind einzelne Aufrufe.
    std::size_t read_attempts{};
    std::size_t reads_succeeded{};
    std::size_t reads_failed{};
    std::string error;
};

// Verbindungsparameter eines Poll-Durchlaufs; Port und Unit-ID haben
// gebrauchstaugliche Standardwerte.
struct PollConnection {
    std::string host;
    int port{502};
    int unit_id{1};
};

// Liest einen kompletten Satz Wechselrichterregister und leitet die Werte an
// die Ausgabeziele weiter. CsvWriter* ist optional und wird nicht besessen;
// ConsoleReporter& muss fuer die gesamte Lebensdauer des Pollers gueltig sein.
class Sun2000Poller {
public:
    Sun2000Poller(PollConnection connection, CsvWriter* csv,
                  ConsoleReporter& console);

    // Liefert Verbindungsstatus, Lesezaehler und einen fatalen Durchlauffehler.
    // Einzelne fehlgeschlagene Registerlesungen werden gezaehlt, der Poll laeuft
    // aber weiter, damit die verfuegbaren Messwerte trotzdem ausgegeben werden.
    PollResult poll_once();

private:
    PollConnection connection_;
    CsvWriter* csv_;
    ConsoleReporter& console_;
};

}  // namespace sun2000
