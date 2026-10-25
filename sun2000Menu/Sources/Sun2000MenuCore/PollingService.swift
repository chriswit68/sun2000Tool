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
    private var lastCorruptCSVPath: String?
    private var lastCorruptRowCount = 0

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
            return "\(updated) – \(skippedCorruptRows) neue CSV-Zeile(n) mit NUL-Zeichen ignoriert."
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
        case .success(let sample, let csvPath):
            values = sample.values
            let newlySkippedRows: Int
            if lastCorruptCSVPath == csvPath {
                newlySkippedRows = max(
                    0,
                    sample.skippedCorruptRows - lastCorruptRowCount
                )
            } else {
                newlySkippedRows = sample.skippedCorruptRows
            }
            lastCorruptCSVPath = csvPath
            lastCorruptRowCount = sample.skippedCorruptRows
            state = .updated(Date(), skippedCorruptRows: newlySkippedRows)
        case .failure(let message):
            state = .failed(message)
        }
    }
}

private enum PollResult: Sendable {
    case success(TelemetryCSVSample, csvPath: String)
    case failure(String)
}

private func executePoll(csvBaseURL: URL) -> PollResult {
    do {
        let csvURL: URL
        if let latestSnapshot = try TelemetryCSVFile.latestSnapshotURL(
            baseURL: csvBaseURL
        ) {
            // Snapshots have immutable names so SMB readers never open a file
            // that sun2000Tool is replacing during the next poll.
            csvURL = latestSnapshot
        } else if FileManager.default.fileExists(
            atPath: TelemetryCSVFile.snapshotDirectoryURL(baseURL: csvBaseURL)
                .path
        ) {
            return .failure(
                "Im Poll-Snapshot-Verzeichnis wurde noch kein vollstaendiger "
                    + "Snapshot gefunden."
            )
        } else if let latestHourly = try TelemetryCSVFile.latestHourlyURL(
            baseURL: csvBaseURL
        ) {
            csvURL = latestHourly
        } else {
            return .failure(
                "Keine Poll-Snapshots oder Stunden-CSV zur konfigurierten "
                    + "Basisdatei gefunden."
            )
        }
        let contents = try String(contentsOf: csvURL, encoding: .utf8)
        let sample = try TelemetryCSVParser.latestSampleWithDiagnostics(
            from: contents
        )
        guard !sample.values.isEmpty else {
            return .failure("Die neueste Stunden-CSV enthaelt noch keine Messwerte.")
        }
        return .success(sample, csvPath: csvURL.path)
    } catch {
        return .failure("CSV-Datei konnte nicht eingelesen werden: \(error.localizedDescription)")
    }
}
