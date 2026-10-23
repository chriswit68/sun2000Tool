import Combine
import Foundation

@MainActor
public final class AppSettings: ObservableObject {
    private enum Key {
        static let toolPath = "sun2000ToolPath"
        static let host = "host"
        static let port = "port"
        static let unitID = "unitID"
        static let intervalSeconds = "intervalSeconds"
        static let selectedSignals = "selectedSignals"
    }

    public static let defaultSelectedSignals = ["active_power", "battery_soc"]

    private let defaults: UserDefaults

    @Published public var toolPath: String {
        didSet { defaults.set(toolPath, forKey: Key.toolPath) }
    }

    @Published public var host: String {
        didSet { defaults.set(host, forKey: Key.host) }
    }

    @Published public var port: Int {
        didSet { defaults.set(port, forKey: Key.port) }
    }

    @Published public var unitID: Int {
        didSet { defaults.set(unitID, forKey: Key.unitID) }
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
        toolPath = defaults.string(forKey: Key.toolPath) ?? ""
        host = defaults.string(forKey: Key.host) ?? ""
        port = defaults.object(forKey: Key.port) as? Int ?? 502
        unitID = defaults.object(forKey: Key.unitID) as? Int ?? 1
        intervalSeconds = defaults.object(forKey: Key.intervalSeconds) as? Int ?? 10
        selectedSignalsStorage = defaults.stringArray(forKey: Key.selectedSignals) ??
            Self.defaultSelectedSignals
    }

    public var validationMessage: String? {
        guard !toolPath.isEmpty else {
            return "Waehle den Pfad zur sun2000Tool-Anwendung."
        }
        guard FileManager.default.isExecutableFile(atPath: toolPath) else {
            return "Die angegebene sun2000Tool-Datei ist nicht ausfuehrbar."
        }
        guard !host.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return "Gib die IP-Adresse oder den Hostnamen des Wechselrichters an."
        }
        guard (1...65535).contains(port) else {
            return "Der Modbus-Port muss zwischen 1 und 65535 liegen."
        }
        guard (0...247).contains(unitID) else {
            return "Die Unit-ID muss zwischen 0 und 247 liegen."
        }
        guard (1...86400).contains(intervalSeconds) else {
            return "Das Abfrageintervall muss zwischen 1 und 86400 Sekunden liegen."
        }
        return nil
    }
}
