import Foundation

public struct TelemetryValue: Equatable, Identifiable, Sendable {
    public let timestamp: String
    public let signal: String
    public let value: String
    public let unit: String

    public var id: String { signal }

    public var displayValue: String {
        if value.hasSuffix(".000000") {
            return String(value.dropLast(7))
        }
        return value
    }
}

public enum TelemetryCSVError: LocalizedError {
    case invalidHeader
    case malformedRow(Int)
    case unterminatedQuotedField

    public var errorDescription: String? {
        switch self {
        case .invalidHeader:
            return "Die CSV-Datei hat keine gueltige sun2000Tool-Kopfzeile."
        case .malformedRow(let row):
            return "Die CSV-Zeile \(row) hat nicht die erwarteten vier Spalten."
        case .unterminatedQuotedField:
            return "Die CSV-Datei enthaelt ein nicht geschlossenes Anfuehrungszeichen."
        }
    }
}

public enum TelemetryCSVParser {
    public static func latestSample(from contents: String) throws -> [TelemetryValue] {
        let rows = try parseRows(contents)
        guard rows.first == ["timestamp_utc", "signal", "value", "unit"] else {
            throw TelemetryCSVError.invalidHeader
        }

        let records = try rows.dropFirst().enumerated().map { offset, fields in
            guard fields.count == 4 else {
                throw TelemetryCSVError.malformedRow(offset + 2)
            }
            return TelemetryValue(
                timestamp: fields[0],
                signal: fields[1],
                value: fields[2],
                unit: fields[3]
            )
        }
        guard let latestTimestamp = records.map(\.timestamp).max() else {
            return []
        }

        var latestBySignal: [String: TelemetryValue] = [:]
        for record in records where record.timestamp == latestTimestamp {
            latestBySignal[record.signal] = record
        }
        return latestBySignal.values.sorted { $0.signal < $1.signal }
    }

    private static func parseRows(_ contents: String) throws -> [[String]] {
        var rows: [[String]] = []
        var row: [String] = []
        var field = ""
        var quoted = false
        var index = contents.startIndex

        while index < contents.endIndex {
            let character = contents[index]
            let nextIndex = contents.index(after: index)

            if quoted {
                if character == "\"" {
                    if nextIndex < contents.endIndex && contents[nextIndex] == "\"" {
                        field.append("\"")
                        index = contents.index(after: nextIndex)
                        continue
                    }
                    quoted = false
                } else {
                    field.append(character)
                }
            } else {
                switch character {
                case "\"" where field.isEmpty:
                    quoted = true
                case ",":
                    row.append(field)
                    field = ""
                case "\n":
                    row.append(field)
                    rows.append(row)
                    row = []
                    field = ""
                case "\r":
                    if nextIndex < contents.endIndex && contents[nextIndex] == "\n" {
                        row.append(field)
                        rows.append(row)
                        row = []
                        field = ""
                        index = contents.index(after: nextIndex)
                        continue
                    }
                    field.append(character)
                default:
                    field.append(character)
                }
            }
            index = nextIndex
        }

        guard !quoted else {
            throw TelemetryCSVError.unterminatedQuotedField
        }
        if !field.isEmpty || !row.isEmpty {
            row.append(field)
            rows.append(row)
        }
        return rows
    }
}

public enum TelemetrySignalCatalog {
    public static func label(for signal: String) -> String {
        let labels = [
            "active_power": "AC-Leistung",
            "battery_soc": "Batterie-Ladezustand",
            "battery_power": "Batterieleistung",
            "daily_yield_energy": "Tagesertrag",
            "device_status": "Geraetestatus",
            "efficiency": "Wirkungsgrad",
            "fault_code": "Fehlercode",
            "firmware_version": "Firmware",
            "grid_frequency": "Netzfrequenz",
            "input_power": "PV-Eingangsleistung",
            "internal_temperature": "Innentemperatur",
            "model_id": "Modell-ID",
            "model_name": "Modell",
            "power_factor": "Leistungsfaktor",
            "product_code": "Produktcode",
            "reactive_power": "Blindleistung",
            "serial_number": "Seriennummer"
        ]
        if let label = labels[signal] {
            return label
        }
        return signal
            .split(separator: "_")
            .map { $0.capitalized }
            .joined(separator: " ")
    }

    public static func shortLabel(for signal: String) -> String {
        let labels = [
            "active_power": "P",
            "battery_soc": "SOC",
            "battery_power": "B",
            "battery_status": "BS",
            "daily_yield_energy": "Tag",
            "device_status": "Stat",
            "efficiency": "Eff",
            "fault_code": "Err",
            "firmware_version": "FW",
            "grid_frequency": "Hz",
            "input_power": "PV",
            "internal_temperature": "T",
            "model_id": "ID",
            "model_name": "Mod",
            "model_name_individual": "Mod",
            "power_factor": "PF",
            "product_code": "Prod",
            "reactive_power": "Q",
            "serial_number": "SN"
        ]
        if let label = labels[signal] {
            return label
        }
        if signal.hasPrefix("pv_") {
            let components = signal.split(separator: "_")
            if components.count == 3,
               components[1].allSatisfy(\.isNumber) {
                let suffix = components[2] == "voltage" ? "V" : "A"
                return "\(components[1])\(suffix)"
            }
        }
        if signal.hasPrefix("battery_unit_") {
            let components = signal.split(separator: "_")
            if components.count >= 4, components[2].allSatisfy(\.isNumber) {
                let suffix = components[3].first.map(String.init) ?? "B"
                return "B\(components[2])\(suffix.uppercased())"
            }
        }

        let acronym = signal
            .split(separator: "_")
            .compactMap(\.first)
            .map(String.init)
            .joined()
            .uppercased()
        return String(acronym.prefix(4))
    }

    public static func group(for signal: String) -> String {
        if signal == "model_name" || signal == "model_name_individual" ||
            signal == "serial_number" || signal == "product_code" ||
            signal == "firmware_version" || signal == "model_id" {
            return "Identifikation"
        }
        if signal.hasPrefix("pv_") {
            return "PV-Eingaenge"
        }
        if signal.hasPrefix("battery_") {
            return "Batterie"
        }
        return "Wechselrichter und Netz"
    }
}
