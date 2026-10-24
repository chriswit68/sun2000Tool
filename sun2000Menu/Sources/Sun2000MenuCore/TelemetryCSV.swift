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

public struct TelemetryCSVSample: Equatable, Sendable {
    public let values: [TelemetryValue]
    public let skippedCorruptRows: Int
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
        try latestSampleWithDiagnostics(from: contents).values
    }

    public static func latestSampleWithDiagnostics(
        from contents: String
    ) throws -> TelemetryCSVSample {
        let physicalRows = contents.split(
            separator: "\n",
            omittingEmptySubsequences: false
        )
        let validRows = physicalRows.filter { !$0.contains("\0") }
        let skippedCorruptRows = physicalRows.count - validRows.count
        let sanitizedContents = validRows.joined(separator: "\n")
        let rows = try parseRows(sanitizedContents)
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
            return TelemetryCSVSample(
                values: [],
                skippedCorruptRows: skippedCorruptRows
            )
        }

        var latestBySignal: [String: TelemetryValue] = [:]
        for record in records where record.timestamp == latestTimestamp {
            latestBySignal[record.signal] = record
        }
        return TelemetryCSVSample(
            values: latestBySignal.values.sorted { $0.signal < $1.signal },
            skippedCorruptRows: skippedCorruptRows
        )
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

public enum TelemetryCSVFile {
    public static func baseURL(from selectedURL: URL) -> URL {
        let filename = selectedURL.deletingPathExtension().lastPathComponent
        guard let separator = filename.lastIndex(of: "_") else {
            return selectedURL
        }
        let hour = String(filename[filename.index(after: separator)...])
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        formatter.dateFormat = "yyyy-MM-dd'T'HH"
        guard hour.count == 13,
              let parsed = formatter.date(from: hour),
              formatter.string(from: parsed) == hour else {
            return selectedURL
        }

        let stem = String(filename[..<separator])
        return selectedURL
            .deletingLastPathComponent()
            .appendingPathComponent("\(stem).\(selectedURL.pathExtension)")
    }

    public static func hourlyURL(baseURL: URL, at date: Date) -> URL {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        formatter.dateFormat = "yyyy-MM-dd'T'HH"
        let hour = formatter.string(from: date)
        let filename =
            "\(baseURL.deletingPathExtension().lastPathComponent)_\(hour).\(baseURL.pathExtension)"
        return baseURL.deletingLastPathComponent().appendingPathComponent(filename)
    }

    public static func latestHourlyURL(baseURL: URL) throws -> URL? {
        let directory = baseURL.deletingLastPathComponent()
        let prefix = "\(baseURL.deletingPathExtension().lastPathComponent)_"
        let suffix = ".\(baseURL.pathExtension)"
        let filenames = try FileManager.default.contentsOfDirectory(
            atPath: directory.path
        )
        let matching = filenames.filter { filename in
            guard filename.hasPrefix(prefix), filename.hasSuffix(suffix) else {
                return false
            }
            let hour = filename.dropFirst(prefix.count).dropLast(suffix.count)
            guard hour.count == 13 else { return false }
            let formatter = DateFormatter()
            formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.calendar = Calendar(identifier: .gregorian)
            formatter.timeZone = TimeZone(secondsFromGMT: 0)
            formatter.dateFormat = "yyyy-MM-dd'T'HH"
            guard let parsed = formatter.date(from: String(hour)) else {
                return false
            }
            return formatter.string(from: parsed) == String(hour)
        }
        guard let latest = matching.max() else { return nil }
        return directory.appendingPathComponent(latest)
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

    public static func description(for signal: String) -> String {
        let descriptions = [
            "active_power": "Momentane Wirkleistung, die der Wechselrichter ins Hausnetz oder Stromnetz abgibt. Negative Werte koennen je nach Betriebsart einen Leistungsfluss in die Gegenrichtung anzeigen.",
            "active_power_fast": "Schnell erfasste momentane Wirkleistung des Wechselrichters.",
            "accumulated_yield_energy": "Gesamte vom Wechselrichter erzeugte Energie seit Beginn des Energiezaehlers.",
            "battery_bus_current": "Momentane Stromstaerke am gemeinsamen Batterieanschluss.",
            "battery_bus_voltage": "Momentane Spannung am gemeinsamen Batterieanschluss.",
            "battery_daily_charge_energy": "Energie, die der Batteriespeicher heute insgesamt aufgenommen hat.",
            "battery_daily_discharge_energy": "Energie, die der Batteriespeicher heute insgesamt abgegeben hat.",
            "battery_power": "Momentane Lade- oder Entladeleistung des gesamten Batteriespeichers. Das Vorzeichen zeigt die Richtung gemaess Geraetekonvention.",
            "battery_rated_capacity": "Nennkapazitaet des Batteriespeichers in Wattstunden.",
            "battery_soc": "Ladezustand des gesamten Batteriespeichers in Prozent.",
            "battery_status": "Numerischer Gesamtstatus des Batteriespeichers. Die Zahl ist ein Geraetecode und muss fuer eine genaue Bedeutung anhand der passenden Registerliste entschluesselt werden.",
            "battery_total_charge": "Vom Batteriespeicher insgesamt aufgenommene Energie seit Beginn des Zaehlers.",
            "battery_total_discharge": "Vom Batteriespeicher insgesamt abgegebene Energie seit Beginn des Zaehlers.",
            "daily_yield_energy": "Energie, die der Wechselrichter seit Mitternacht erzeugt hat.",
            "device_status": "Numerischer Betriebszustand des Wechselrichters. Die Zahl ist ein Geraetecode, kein direkt lesbarer Text.",
            "efficiency": "Vom Wechselrichter gemeldeter Wirkungsgrad: welcher Anteil der aufgenommenen Energie als nutzbare Leistung herauskommt.",
            "fault_code": "Numerischer Fehlercode des Wechselrichters. Auch der Wert 0 ist meist ein gueltiger Code und bedeutet typischerweise, dass kein Fehler gemeldet wird.",
            "firmware_version": "Versionskennung der Wechselrichter-Firmware.",
            "grid_frequency": "Frequenz des Stromnetzes, in Deutschland normalerweise nahe 50 Hertz.",
            "input_power": "Momentane gesamte Leistung, die der Wechselrichter von den Solarmodulen auf der Gleichstromseite aufnimmt.",
            "internal_temperature": "Vom Wechselrichter gemeldete interne Temperatur.",
            "insulation_resistance": "Messwert fuer den Isolationswiderstand der Gleichstromseite; dient der elektrischen Sicherheitsueberwachung.",
            "model_id": "Numerische Kennung des Wechselrichtermodells. Sie ist nicht selbst der lesbare Modellname.",
            "model_name": "Lesbare Modellbezeichnung des Wechselrichters.",
            "model_name_individual": "Modellbezeichnung aus einem alternativen Zugriff auf einzelne Register.",
            "mppt_count": "Anzahl der unabhaengigen Regler, die die Solarmodule auf ihren jeweils besten Arbeitspunkt einstellen.",
            "monthly_yield_energy": "Energie, die der Wechselrichter seit Beginn des aktuellen Monats erzeugt hat.",
            "power_factor": "Leistungsfaktor: beschreibt, wie wirksam Strom und Spannung zusammen elektrische Leistung uebertragen. Nahe 1 bedeutet ublicherweise einen hohen Wirkleistungsanteil.",
            "product_code": "Produkt- oder Ausfuehrungskennung des Wechselrichters.",
            "product_sales_area": "Kennung der Verkaufsregion, fuer die das Produkt vorgesehen ist.",
            "reactive_power": "Blindleistung. Sie pendelt zwischen Geraet und Stromnetz und unterstuetzt unter anderem die Netzspannungsregelung.",
            "serial_number": "Eindeutige Seriennummer des Wechselrichters.",
            "software_version": "Versionskennung der Wechselrichter-Software.",
            "startup_time": "Zeitpunkt des letzten Starts des Wechselrichters, als Sekundenwert und zusaetzlich lesbare UTC-Zeit.",
            "shutdown_time": "Zeitpunkt des letzten Stopps des Wechselrichters. Ein Platzhalter kann bedeuten, dass kein gueltiger Zeitpunkt vorliegt.",
            "total_dc_input_energy": "Gesamte Energie, die der Wechselrichter auf der PV-Gleichstromseite aufgenommen hat.",
            "yearly_yield_energy": "Energie, die der Wechselrichter seit Beginn des aktuellen Jahres erzeugt hat."
        ]
        if let description = descriptions[signal] {
            return description
        }

        if signal.hasPrefix("pv_") {
            if signal.hasSuffix("_voltage") {
                return "Momentane Gleichspannung des PV-Strings. Ein String ist eine Reihe miteinander verbundener Solarmodule."
            }
            if signal.hasSuffix("_current") {
                return "Momentane Stromstaerke dieses PV-Strings. 0 A kann ein gueltiger Wert sein, wenn gerade kein Strom fliesst."
            }
        }

        if signal.hasPrefix("phase_voltage_") {
            return "Spannung einer einzelnen Netzphase (A, B oder C) gegen den Neutralleiter."
        }
        if signal.hasPrefix("phase_current_") {
            return "Momentane Stromstaerke auf der angegebenen Netzphase (A, B oder C)."
        }
        if signal.hasPrefix("line_voltage_") {
            return "Spannung zwischen den beiden im Signalnamen genannten Netzphasen."
        }
        if signal.hasPrefix("battery_unit_") {
            if signal.hasSuffix("_serial_number") {
                return "Eindeutige Seriennummer dieser Batterieeinheit."
            }
            if signal.hasSuffix("_soc") {
                return "Ladezustand dieser Batterieeinheit in Prozent."
            }
            if signal.hasSuffix("_power") {
                return "Momentane Lade- oder Entladeleistung dieser Batterieeinheit. Das Vorzeichen folgt der Geraetekonvention."
            }
            if signal.hasSuffix("_bus_voltage") {
                return "Momentane Spannung am Anschluss dieser Batterieeinheit."
            }
            if signal.hasSuffix("_bus_current") {
                return "Momentane Stromstaerke am Anschluss dieser Batterieeinheit."
            }
            if signal.hasSuffix("_temperature") {
                return "Temperaturmesswert dieser Batterieeinheit."
            }
            if signal.contains("_daily_charge_energy") {
                return "Energie, die diese Batterieeinheit heute aufgenommen hat."
            }
            if signal.contains("_daily_discharge_energy") {
                return "Energie, die diese Batterieeinheit heute abgegeben hat."
            }
            if signal.contains("_total_charge") {
                return "Gesamtenergie, die diese Batterieeinheit seit Beginn des Zaehlers aufgenommen hat."
            }
            if signal.contains("_total_discharge") {
                return "Gesamtenergie, die diese Batterieeinheit seit Beginn des Zaehlers abgegeben hat."
            }
            if signal.contains("_version") {
                return "Software- oder Firmware-Version einer Baugruppe dieser Batterieeinheit."
            }
            if signal.contains("_status") || signal.contains("_fault_id") ||
                signal.contains("_working_mode") {
                return "Numerischer Status-, Fehler- oder Betriebsmodus dieser Batterieeinheit. Die genaue Bedeutung haengt vom Geraetecode ab."
            }
            if signal.contains("_rated_") || signal.contains("_maximum_") {
                return "Nenn- oder Grenzwert der Lade- beziehungsweise Entladeleistung; dies ist nicht die aktuelle Leistung."
            }
            if signal.contains("_soh_calibration_status") {
                return "Status der SOH-Kalibrierung dieses Batterie-Packs. SOH ist eine Schaetzung des Batteriezustands."
            }
        }

        if signal.hasSuffix("_temperature") ||
            signal.range(of: #"^internal_temperature_[0-9]+$"#,
                         options: .regularExpression) != nil {
            return "Temperaturmesswert eines internen Messpunkts des Wechselrichters oder Speichers."
        }
        if signal.contains("alarm") || signal.hasSuffix("_status") ||
            signal.hasSuffix("_flags") || signal.contains("bitfield") {
            return "Status- oder Alarmwert. Bei einem Bitfeld enthaelt die Zahl mehrere einzelne Ein/Aus-Informationen; die Bits muessen anhand der Registerliste interpretiert werden."
        }
        if signal.hasSuffix("_time") || signal.hasSuffix("_time_utc") {
            return "Zeitmarke eines Betriebs- oder Statistikereignisses. UTC-Zeitfelder sind lesbar formatiert; bei Rohwerten handelt es sich um Sekunden seit einem Zeitursprung."
        }
        if signal.contains("_energy") || signal.contains("_yield_energy") {
            return "Energiemenge fuer den im Signalnamen genannten Zeitraum oder Zaehler."
        }
        if signal.hasSuffix("_voltage") {
            return "Spannungsmesswert. Der Signalname benennt den gemessenen Stromkreis oder Messpunkt."
        }
        if signal.hasSuffix("_current") {
            return "Stromstaerkemesswert. Der Signalname benennt den gemessenen Stromkreis oder Messpunkt."
        }
        if signal.contains("power") {
            return "Leistungswert oder Leistungsgrenze des Wechselrichters. Grenzwerte sind keine momentanen Messwerte."
        }
        if signal.contains("version") {
            return "Versionskennung einer Geraetesoftware oder internen Baugruppe."
        }
        if signal.contains("alarm") || signal.contains("status") ||
            signal.contains("flags") || signal.hasSuffix("_code") ||
            signal.hasSuffix("_id") {
            return "Numerischer Status-, Alarm- oder Kennwert. Die konkrete Bedeutung ergibt sich aus der passenden Registerliste."
        }
        return "Technischer Diagnose- oder Messwert. Die genaue Bedeutung haengt von der Geraete- und Firmwareversion ab."
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

    public static func menuBarValue(for value: TelemetryValue) -> String {
        let isCurrent = value.signal.hasSuffix("_current") ||
            value.signal.hasPrefix("phase_current_")
        let isTemperature = value.signal.contains("_temperature")
        guard (isCurrent || isTemperature),
              let numericValue = Double(value.value) else {
            return value.displayValue
        }
        return String(format: "%.1f", locale: Locale.current, numericValue)
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
