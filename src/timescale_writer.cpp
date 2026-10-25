#include "timescale_writer.hpp"

#include <fstream>
#include <exception>
#include <cmath>
#include <iterator>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <libpq-fe.h>

namespace sun2000 {
namespace {

std::string quote_conninfo_value(const std::string& value) {
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\\' || character == '\'') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('\'');
    return quoted;
}

std::string make_connection_string(const TimescaleConfig& config) {
    if (config.host.empty() || config.database.empty() || config.user.empty() ||
        config.password.empty() || config.port < 1 || config.port > 65535) {
        throw std::invalid_argument(
            "TimescaleDB-Verbindungsdaten sind unvollstaendig oder ungueltig.");
    }
    return "host=" + quote_conninfo_value(config.host) +
           " port=" + std::to_string(config.port) +
           " dbname=" + quote_conninfo_value(config.database) +
           " user=" + quote_conninfo_value(config.user) +
           " password=" + quote_conninfo_value(config.password) +
           " connect_timeout=5";
}

std::string connection_error(PGconn* connection) {
    const char* message = PQerrorMessage(connection);
    return message == nullptr ? "unbekannter PostgreSQL-Fehler" : message;
}

void execute_command(PGconn* connection, const char* sql) {
    PGresult* result = PQexec(connection, sql);
    if (result == nullptr) {
        throw std::runtime_error(connection_error(connection));
    }
    const auto status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        const char* message = PQresultErrorMessage(result);
        const std::string error =
            message == nullptr ? connection_error(connection) : message;
        PQclear(result);
        throw std::runtime_error(error);
    }
    PQclear(result);
}

void insert_record(PGconn* connection, const TelemetryRecord& record) {
    std::istringstream numeric_input(record.value);
    numeric_input.imbue(std::locale::classic());
    double numeric_value = 0;
    const bool has_numeric_value =
        static_cast<bool>(numeric_input >> numeric_value) &&
        numeric_input.peek() == std::char_traits<char>::eof() &&
        std::isfinite(numeric_value);
    const std::string numeric_text =
        has_numeric_value ? record.value : std::string();
    const char* values[] = {record.timestamp.c_str(), record.signal.c_str(),
                            record.value.c_str(),
                            has_numeric_value ? numeric_text.c_str() : nullptr,
                            record.unit.c_str()};
    PGresult* result = PQexecParams(
        connection,
        "INSERT INTO sun2000_telemetry "
        "(timestamp, signal, value, numeric_value, unit) "
        "VALUES ($1::timestamptz, $2, $3, $4::double precision, $5) "
        "ON CONFLICT (timestamp, signal) DO UPDATE "
        "SET value = EXCLUDED.value, numeric_value = EXCLUDED.numeric_value, "
        "unit = EXCLUDED.unit",
        5, nullptr, values, nullptr, nullptr, 0);
    if (result == nullptr) {
        throw std::runtime_error(connection_error(connection));
    }
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        const char* message = PQresultErrorMessage(result);
        const std::string error =
            message == nullptr ? connection_error(connection) : message;
        PQclear(result);
        throw std::runtime_error(error);
    }
    PQclear(result);
}

void rollback_after_failure(PGconn* connection) {
    const auto original_error = std::current_exception();
    try {
        execute_command(connection, "ROLLBACK");
    } catch (const std::exception& rollback_error) {
        throw std::runtime_error(std::string("Transaktion fehlgeschlagen; "
                                             "Rollback ebenfalls fehlgeschlagen: ") +
                                 rollback_error.what());
    } catch (...) {
        throw std::runtime_error(
            "Transaktion fehlgeschlagen; Rollback schlug mit unbekanntem Fehler fehl.");
    }
    std::rethrow_exception(original_error);
}

std::vector<std::vector<std::string>> parse_csv(const std::string& contents) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool closed_quote = false;

    for (std::size_t index = 0; index < contents.size(); ++index) {
        const char character = contents[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1 < contents.size() && contents[index + 1] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    quoted = false;
                    closed_quote = true;
                }
            } else {
                field.push_back(character);
            }
            continue;
        }

        if (closed_quote && character != ',' && character != '\n' &&
            character != '\r') {
            throw std::runtime_error("Ungueltiges Zeichen nach CSV-Anfuehrungszeichen.");
        }
        switch (character) {
            case '"':
                if (!field.empty() || closed_quote) {
                    throw std::runtime_error("Ungueltiges CSV-Anfuehrungszeichen.");
                }
                quoted = true;
                break;
            case ',':
                row.push_back(std::move(field));
                field.clear();
                closed_quote = false;
                break;
            case '\n':
                row.push_back(std::move(field));
                field.clear();
                rows.push_back(std::move(row));
                row.clear();
                closed_quote = false;
                break;
            case '\r':
                if (index + 1 < contents.size() && contents[index + 1] == '\n') {
                    row.push_back(std::move(field));
                    field.clear();
                    rows.push_back(std::move(row));
                    row.clear();
                    closed_quote = false;
                    ++index;
                } else {
                    field.push_back(character);
                    closed_quote = false;
                }
                break;
            default:
                field.push_back(character);
                break;
        }
    }
    if (quoted) {
        throw std::runtime_error("CSV enthaelt ein nicht geschlossenes Feld.");
    }
    if (!field.empty() || !row.empty() || closed_quote) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace

class TimescaleWriter::Impl {
public:
    explicit Impl(const TimescaleConfig& config)
        : connection(PQconnectdb(make_connection_string(config).c_str()),
                     &PQfinish) {
        if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
            const std::string error =
                connection ? connection_error(connection.get())
                           : "PostgreSQL-Verbindung konnte nicht angelegt werden.";
            throw std::runtime_error("TimescaleDB-Verbindung fehlgeschlagen: " +
                                     error);
        }
        execute_command(connection.get(), "BEGIN");
        try {
            execute_command(connection.get(),
                            "CREATE EXTENSION IF NOT EXISTS timescaledb");
            execute_command(
                connection.get(),
            "CREATE TABLE IF NOT EXISTS sun2000_telemetry ("
            "timestamp timestamptz NOT NULL, "
            "signal text NOT NULL, "
            "value text NOT NULL, "
            "numeric_value double precision, "
            "unit text NOT NULL, "
                "PRIMARY KEY (timestamp, signal))");
            execute_command(connection.get(),
                            "ALTER TABLE sun2000_telemetry "
                            "ADD COLUMN IF NOT EXISTS numeric_value "
                            "double precision");
            execute_command(
                connection.get(),
                "SELECT create_hypertable('sun2000_telemetry', 'timestamp', "
                "if_not_exists => TRUE)");
            execute_command(connection.get(), "COMMIT");
        } catch (...) {
            rollback_after_failure(connection.get());
        }
    }

    std::unique_ptr<PGconn, decltype(&PQfinish)> connection;
};

TimescaleWriter::TimescaleWriter(const TimescaleConfig& config)
    : implementation_(std::make_unique<Impl>(config)) {}

TimescaleWriter::~TimescaleWriter() = default;

std::vector<TelemetryRecord> TimescaleWriter::read_csv_records(
    const std::string& csv_path) {
    std::ifstream input(csv_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("CSV-Stundendatei kann nicht gelesen werden: " +
                                 csv_path);
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    const auto rows = parse_csv(contents);
    if (rows.empty() ||
        rows.front() !=
            std::vector<std::string>{"timestamp_utc", "signal", "value", "unit"}) {
        throw std::runtime_error("CSV-Stundendatei hat einen ungueltigen Header.");
    }

    std::vector<TelemetryRecord> records;
    records.reserve(rows.size() - 1);
    for (std::size_t index = 1; index < rows.size(); ++index) {
        const auto& fields = rows[index];
        if (fields.size() == 1 && fields[0].empty()) {
            continue;
        }
        if (fields.size() != 4) {
            throw std::runtime_error("CSV-Datensatz " +
                                     std::to_string(index + 1) +
                                     " hat nicht vier Spalten.");
        }
        records.push_back({fields[0], fields[1], fields[2], fields[3]});
    }
    return records;
}

std::size_t TimescaleWriter::store_csv_file(const std::string& csv_path) {
    const auto records = read_csv_records(csv_path);
    if (records.empty()) {
        throw std::runtime_error(
            "CSV-Stundendatei enthaelt keine Messdatensaetze.");
    }

    PGconn* connection = implementation_->connection.get();
    if (PQstatus(connection) != CONNECTION_OK) {
        PQreset(connection);
        if (PQstatus(connection) != CONNECTION_OK) {
            throw std::runtime_error("TimescaleDB-Verbindung kann nicht "
                                     "wiederhergestellt werden: " +
                                     connection_error(connection));
        }
    }
    execute_command(connection, "BEGIN");
    try {
        for (const auto& record : records) {
            insert_record(connection, record);
        }
        execute_command(connection, "COMMIT");
    } catch (...) {
        rollback_after_failure(connection);
    }
    return records.size();
}

}  // namespace sun2000
