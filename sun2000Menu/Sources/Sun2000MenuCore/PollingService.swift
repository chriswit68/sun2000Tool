import Combine
import Foundation

public enum PollingState: Equatable {
    case idle
    case polling
    case updated(Date, skippedCorruptRows: Int)
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
            return "Noch keine CSV-Datei eingelesen."
        case .polling:
            return "CSV-Datei wird eingelesen …"
        case .updated(let date, let skippedCorruptRows):
            let updated = "Zuletzt aktualisiert: \(date.formatted(date: .omitted, time: .shortened))"
            guard skippedCorruptRows > 0 else { return updated }
            return "\(updated) – \(skippedCorruptRows) beschaedigte CSV-Zeile(n) mit NUL-Zeichen ignoriert."
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
        let csvBaseURL = URL(fileURLWithPath: settings.csvBasePath)
        let result = await Task.detached(priority: .utility) {
            executePoll(csvBaseURL: csvBaseURL)
        }.value
        isPolling = false

        switch result {
        case .success(let sample):
            values = sample.values
            state = .updated(Date(), skippedCorruptRows: sample.skippedCorruptRows)
        case .failure(let message):
            state = .failed(message)
        }
    }
}

private enum PollResult: Sendable {
    case success(TelemetryCSVSample)
    case failure(String)
}

private func executePoll(csvBaseURL: URL) -> PollResult {
    do {
        guard let csvURL = try TelemetryCSVFile.latestHourlyURL(
            baseURL: csvBaseURL
        ) else {
            return .failure("Keine Stunden-CSV zur konfigurierten Basisdatei gefunden.")
        }
        let contents = try String(contentsOf: csvURL, encoding: .utf8)
        let sample = try TelemetryCSVParser.latestSampleWithDiagnostics(
            from: contents
        )
        guard !sample.values.isEmpty else {
            return .failure("Die neueste Stunden-CSV enthaelt noch keine Messwerte.")
        }
        return .success(sample)
    } catch {
        return .failure("CSV-Datei konnte nicht eingelesen werden: \(error.localizedDescription)")
    }
}
