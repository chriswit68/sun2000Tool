import AppKit
import Sun2000MenuCore
import SwiftUI

struct SettingsView: View {
    @ObservedObject var settings: AppSettings
    @ObservedObject var poller: PollingService

    var body: some View {
        Form {
            Section("Messwerte aus CSV") {
                HStack {
                    TextField("Pfad zur CSV-Basisdatei", text: $settings.csvBasePath)
                        .textFieldStyle(.roundedBorder)
                    Button("Auswaehlen …", action: chooseCSV)
                }
                Text("Waehle die CSV-Basisdatei, z. B. sun2000.csv. Falls sun2000Tool Poll-Snapshots bereitstellt, liest die App bevorzugt den neuesten Snapshot.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            Section("Aktualisierung") {
                HStack {
                    TextField(
                        "Intervall in Sekunden",
                        value: $settings.intervalSeconds,
                        format: .number
                    )
                    .frame(width: 140)
                    Text("Sekunden")
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("Jetzt aktualisieren") {
                        Task { await poller.pollNow() }
                    }
                    .disabled(poller.isPolling)
                }
                LabeledContent("Status", value: poller.statusDescription)
                    .lineLimit(2)
                    .foregroundStyle(poller.state.isFailure ? .red : .secondary)
            }

            if poller.availableSignals.isEmpty {
                Section("Direkt in der Menueleiste") {
                    Text("Nach der ersten erfolgreichen Abfrage kannst du Werte auswaehlen.")
                        .foregroundStyle(.secondary)
                }
            } else {
                Section("Direkt in der Menueleiste") {
                    Text("Die Menueleiste hat begrenzten Platz. Nicht ausgewaehlte Werte erscheinen im linken Pulldown-Menue.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                ForEach(groupedAvailableSignals, id: \.category) { group in
                    Section(group.category) {
                        ForEach(group.signals, id: \.self) { signal in
                            Toggle(
                                TelemetrySignalCatalog.label(for: signal),
                                isOn: selectionBinding(for: signal)
                            )
                            .help(TelemetrySignalCatalog.description(for: signal))
                        }
                    }
                }
            }

            if let validationMessage = settings.validationMessage {
                Section {
                    Label(validationMessage, systemImage: "exclamationmark.triangle")
                        .foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .formStyle(.grouped)
        .padding()
    }

    private func selectionBinding(for signal: String) -> Binding<Bool> {
        Binding(
            get: { settings.selectedSignals.contains(signal) },
            set: { selected in
                if selected && !settings.selectedSignals.contains(signal) {
                    settings.selectedSignals.append(signal)
                } else if !selected {
                    settings.selectedSignals.removeAll { $0 == signal }
                }
            }
        )
    }

    private var groupedAvailableSignals: [(category: String, signals: [String])] {
        let categories = [
            "Identifikation",
            "PV-Eingaenge",
            "Wechselrichter und Netz",
            "Batterie"
        ]
        return categories.compactMap { category in
            let signals = poller.availableSignals.filter {
                TelemetrySignalCatalog.group(for: $0) == category
            }
            return signals.isEmpty ? nil : (category, signals)
        }
    }

    private func chooseCSV() {
        let panel = NSOpenPanel()
        panel.title = "CSV-Datei auswaehlen"
        panel.message = "Waehle die CSV-Basisdatei oder eine ihrer Stunden-Dateien."
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        NSApp.activate(ignoringOtherApps: true)
        panel.begin { response in
            guard response == .OK, let url = panel.url else { return }
            settings.csvBasePath = TelemetryCSVFile.baseURL(from: url).path
        }
    }
}

private extension PollingState {
    var isFailure: Bool {
        if case .failed = self { return true }
        return false
    }
}
