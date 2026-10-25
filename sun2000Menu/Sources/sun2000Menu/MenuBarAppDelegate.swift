import AppKit
import Combine
import Sun2000MenuCore
import SwiftUI

@MainActor
final class MenuBarAppDelegate: NSObject, NSApplicationDelegate {
    let settings = AppSettings()
    lazy var poller = PollingService(settings: settings)

    private var statusItem: NSStatusItem?
    private var settingsWindow: NSWindow?
    private var subscriptions = Set<AnyCancellable>()

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem = item
        if let button = item.button {
            button.image = NSImage(
                systemSymbolName: "sun.max.fill",
                accessibilityDescription: "SUN2000"
            )
            button.target = self
            button.action = #selector(statusItemClicked(_:))
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }

        settings.objectWillChange
            .sink { [weak self] in
                DispatchQueue.main.async {
                    self?.refreshStatusItem()
                }
            }
            .store(in: &subscriptions)
        poller.objectWillChange
            .sink { [weak self] in
                DispatchQueue.main.async {
                    self?.refreshStatusItem()
                }
            }
            .store(in: &subscriptions)

        refreshStatusItem()
        poller.start()
    }

    func applicationWillTerminate(_ notification: Notification) {
        poller.stop()
    }

    @objc private func statusItemClicked(_ sender: Any?) {
        guard let button = statusItem?.button else { return }
        let menu = NSApp.currentEvent?.type == .rightMouseUp
            ? makeSettingsMenu()
            : makeTelemetryMenu()
        menu.popUp(
            positioning: nil,
            at: NSPoint(x: 0, y: button.bounds.maxY),
            in: button
        )
    }

    @objc func openSettings(_ sender: Any?) {
        let window: NSWindow
        if let existingWindow = settingsWindow {
            window = existingWindow
        } else {
            window = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: 600, height: 640),
                styleMask: [.titled, .closable, .miniaturizable],
                backing: .buffered,
                defer: false
            )
            window.title = "sun2000Menu – Einstellungen"
            window.isReleasedWhenClosed = false
            window.contentView = NSHostingView(
                rootView: SettingsView(settings: settings, poller: poller)
            )
            settingsWindow = window
        }
        NSApp.activate(ignoringOtherApps: true)
        window.center()
        window.makeKeyAndOrderFront(nil)
    }

    @objc private func pollNow(_ sender: Any?) {
        Task {
            await poller.pollNow()
        }
    }

    @objc private func terminate(_ sender: Any?) {
        NSApp.terminate(nil)
    }

    private func makeTelemetryMenu() -> NSMenu {
        let menu = NSMenu()
        menu.autoenablesItems = false
        let selected = Set(settings.selectedSignals)
        let remainingValues = poller.values.filter { !selected.contains($0.signal) }

        if remainingValues.isEmpty {
            let message = poller.values.isEmpty
                ? "Noch keine Messwerte verfuegbar"
                : "Alle verfuegbaren Werte sind in der Menueleiste ausgewaehlt"
            let item = NSMenuItem(title: message, action: nil, keyEquivalent: "")
            item.isEnabled = false
            menu.addItem(item)
        } else {
            let groups = ["Identifikation", "PV-Eingaenge",
                          "Wechselrichter und Netz", "Batterie"]
            for group in groups {
                let groupValues = remainingValues
                    .filter { TelemetrySignalCatalog.group(for: $0.signal) == group }
                    .sorted { $0.signal < $1.signal }
                guard !groupValues.isEmpty else { continue }

                let heading = NSMenuItem(title: group, action: nil, keyEquivalent: "")
                heading.isEnabled = false
                menu.addItem(heading)
                for value in groupValues {
                    let suffix = value.unit.isEmpty ? "" : " \(value.unit)"
                    let item = NSMenuItem(
                        title: "\(TelemetrySignalCatalog.label(for: value.signal))    \(value.displayValue)\(suffix)",
                        action: nil,
                        keyEquivalent: ""
                    )
                    item.isEnabled = false
                    menu.addItem(item)
                }
                menu.addItem(.separator())
            }
        }

        let update = NSMenuItem(
            title: poller.statusDescription,
            action: nil,
            keyEquivalent: ""
        )
        update.isEnabled = false
        menu.addItem(update)
        menu.addItem(.separator())
        menu.addItem(
            NSMenuItem(
                title: "Jetzt aktualisieren",
                action: #selector(pollNow(_:)),
                keyEquivalent: "r"
            ).configured(target: self)
        )
        menu.addItem(
            NSMenuItem(
                title: "Einstellungen …",
                action: #selector(openSettings(_:)),
                keyEquivalent: ","
            ).configured(target: self)
        )
        menu.addItem(.separator())
        menu.addItem(
            NSMenuItem(
                title: "sun2000Menu beenden",
                action: #selector(terminate(_:)),
                keyEquivalent: "q"
            ).configured(target: self)
        )
        return menu
    }

    private func makeSettingsMenu() -> NSMenu {
        let menu = NSMenu()
        menu.autoenablesItems = false
        menu.addItem(
            NSMenuItem(
                title: "Einstellungen …",
                action: #selector(openSettings(_:)),
                keyEquivalent: ""
            ).configured(target: self)
        )
        menu.addItem(
            NSMenuItem(
                title: "Jetzt aktualisieren",
                action: #selector(pollNow(_:)),
                keyEquivalent: ""
            ).configured(target: self)
        )
        menu.addItem(.separator())
        menu.addItem(
            NSMenuItem(
                title: "sun2000Menu beenden",
                action: #selector(terminate(_:)),
                keyEquivalent: ""
            ).configured(target: self)
        )
        return menu
    }

    private func refreshStatusItem() {
        guard let button = statusItem?.button else { return }
        let valuesBySignal = Dictionary(
            uniqueKeysWithValues: poller.values.map { ($0.signal, $0) }
        )
        let parts = settings.selectedSignals.compactMap { signal -> String? in
            guard let value = valuesBySignal[signal] else { return nil }
            let unit = value.unit.isEmpty ? "" : value.unit
            let displayValue = TelemetrySignalCatalog.menuBarValue(for: value)
            return "\(TelemetrySignalCatalog.shortLabel(for: signal)) \(displayValue)\(unit)"
        }
        let title = parts.isEmpty ? "" : " " + parts.joined(separator: "  ·  ")
        button.title = title
        button.toolTip = poller.statusDescription
    }
}

private extension NSMenuItem {
    func configured(target: MenuBarAppDelegate) -> NSMenuItem {
        self.target = target
        isEnabled = true
        return self
    }
}
