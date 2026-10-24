import Combine
import Foundation

@MainActor
public final class AppSettings: ObservableObject {
    private enum Key {
        static let csvBasePath = "csvBasePath"
        static let intervalSeconds = "intervalSeconds"
        static let selectedSignals = "selectedSignals"
    }

    public static let defaultSelectedSignals = ["active_power", "battery_soc"]

    private let defaults: UserDefaults

    @Published public var csvBasePath: String {
        didSet { defaults.set(csvBasePath, forKey: Key.csvBasePath) }
    }

    @Published public var intervalSeconds: Int {
        didSet { defaults.set(intervalSeconds, forKey: Key.intervalSeconds) }
    }

    @Published private var selectedSignalsStorage: [String] {
        didSet {
            defaults.set(selectedSignalsStorage, forKey: Key.selectedSignals)
        }
    }

    public var selectedSignals: [String] {
        get { selectedSignalsStorage }
        set {
            selectedSignalsStorage = newValue.reduce(into: []) { result, signal in
                if !result.contains(signal) {
                    result.append(signal)
                }
            }
        }
    }

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        csvBasePath = defaults.string(forKey: Key.csvBasePath) ?? ""
        intervalSeconds = defaults.object(forKey: Key.intervalSeconds) as? Int ?? 10
        selectedSignalsStorage = defaults.stringArray(forKey: Key.selectedSignals) ??
            Self.defaultSelectedSignals
    }

    public var validationMessage: String? {
        guard !csvBasePath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return "Waehle die CSV-Basisdatei der Messwerte."
        }
        let baseURL = URL(fileURLWithPath: csvBasePath)
        guard baseURL.pathExtension.lowercased() == "csv" else {
            return "Die Basisdatei muss die Endung .csv haben."
        }
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(
            atPath: baseURL.deletingLastPathComponent().path,
            isDirectory: &isDirectory
        ), isDirectory.boolValue else {
            return "Das Verzeichnis der CSV-Basisdatei ist nicht vorhanden."
        }
        guard (1...86400).contains(intervalSeconds) else {
            return "Das Abfrageintervall muss zwischen 1 und 86400 Sekunden liegen."
        }
        return nil
    }
}
