import Combine
import Foundation

public enum PollingState: Equatable {
    case idle
    case polling
    case updated(Date)
    case failed(String)
}

@MainActor
public final class PollingService: ObservableObject {
    @Published public private(set) var values: [TelemetryValue] = []
    @Published public private(set) var state: PollingState = .idle
    @Published public private(set) var isPolling = false

    private let settings: AppSettings
    private var timerTask: Task<Void, Never>?

    public init(settings: AppSettings) {
        self.settings = settings
    }

    public var availableSignals: [String] {
        values.map(\.signal).sorted()
    }

    public var statusDescription: String {
        switch state {
        case .idle:
            return "Noch keine Abfrage durchgefuehrt."
        case .polling:
            return "Wechselrichter wird abgefragt …"
        case .updated(let date):
            return "Zuletzt aktualisiert: \(date.formatted(date: .omitted, time: .shortened))"
        case .failed(let message):
            return message
        }
    }

    public func start() {
        guard timerTask == nil else { return }
        timerTask = Task { [weak self] in
            guard let self else { return }
            while !Task.isCancelled {
                await pollNow()
                let seconds = max(1, settings.intervalSeconds)
                do {
                    try await Task.sleep(for: .seconds(seconds))
                } catch {
                    break
                }
            }
        }
    }

    public func stop() {
        timerTask?.cancel()
        timerTask = nil
    }

    public func pollNow() async {
        guard !isPolling else { return }
        if let validationMessage = settings.validationMessage {
            state = .failed(validationMessage)
            return
        }

        isPolling = true
        state = .polling
        let configuration = PollConfiguration(
            toolURL: URL(fileURLWithPath: settings.toolPath),
            host: settings.host,
            port: settings.port,
            unitID: settings.unitID
        )
        let result = await Task.detached(priority: .utility) {
            executePoll(configuration)
        }.value
        isPolling = false

        switch result {
        case .success(let latestValues):
            values = latestValues
            state = .updated(Date())
        case .failure(let message):
            state = .failed(message)
        }
    }
}

private struct PollConfiguration: Sendable {
    let toolURL: URL
    let host: String
    let port: Int
    let unitID: Int
}

private enum PollResult: Sendable {
    case success([TelemetryValue])
    case failure(String)
}

private func conciseDiagnostics(_ output: String) -> String {
    let lines = output
        .split(whereSeparator: \.isNewline)
        .prefix(4)
        .map(String.init)
    guard !lines.isEmpty else { return "" }
    let summary = lines.joined(separator: "\n")
    return String(summary.prefix(1200))
}

private func executePoll(_ configuration: PollConfiguration) -> PollResult {
    do {
        let applicationSupport = try FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        )
        let dataDirectory = applicationSupport.appendingPathComponent(
            "sun2000Menu",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: dataDirectory,
            withIntermediateDirectories: true
        )
        let csvURL = dataDirectory.appendingPathComponent("latest.csv")
        let errorURL = dataDirectory.appendingPathComponent("poll.stderr")
        for url in [csvURL, errorURL] where
            FileManager.default.fileExists(atPath: url.path) {
            try FileManager.default.removeItem(at: url)
        }
        guard FileManager.default.createFile(
            atPath: errorURL.path,
            contents: nil
        ) else {
            throw NSError(
                domain: "sun2000Menu",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey:
                    "Die Diagnoseausgabe konnte nicht angelegt werden."]
            )
        }

        let process = Process()
        process.executableURL = configuration.toolURL
        process.arguments = [
            "poll",
            "--host", configuration.host,
            "--port", String(configuration.port),
            "--unit-id", String(configuration.unitID),
            "--once",
            "--csv",
            "--log-file", csvURL.path,
            "--console-output", "all"
        ]
        process.standardOutput = FileHandle.nullDevice
        let errorHandle = try FileHandle(forWritingTo: errorURL)
        process.standardError = errorHandle
        try process.run()
        process.waitUntilExit()
        try errorHandle.close()

        let errorOutput = try String(contentsOf: errorURL, encoding: .utf8)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let diagnosticSummary = conciseDiagnostics(errorOutput)
        guard process.terminationReason == .exit, process.terminationStatus == 0 else {
            try? FileManager.default.removeItem(at: errorURL)
            let details = diagnosticSummary.isEmpty
                ? ""
                : "\n\(diagnosticSummary)"
            return .failure(
                "sun2000Tool wurde mit Status \(process.terminationStatus) beendet.\(details)"
            )
        }

        let contents = try String(contentsOf: csvURL, encoding: .utf8)
        let values = try TelemetryCSVParser.latestSample(from: contents)
        guard !values.isEmpty else {
            try? FileManager.default.removeItem(at: errorURL)
            let diagnostics = diagnosticSummary.isEmpty
                ? "Keine Registerwerte empfangen. Pruefe Host, Port und Unit-ID."
                : diagnosticSummary
            return .failure(
                "sun2000Tool hat keine Messwerte in die CSV geschrieben.\n\(diagnostics)"
            )
        }
        try FileManager.default.removeItem(at: errorURL)
        return .success(values)
    } catch {
        return .failure("Abfrage fehlgeschlagen: \(error.localizedDescription)")
    }
}
