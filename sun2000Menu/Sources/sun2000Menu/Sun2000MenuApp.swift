import SwiftUI

@main
struct Sun2000MenuApp: App {
    @NSApplicationDelegateAdaptor(MenuBarAppDelegate.self)
    private var appDelegate

    var body: some Scene {
        Settings {
            EmptyView()
        }
    }
}
