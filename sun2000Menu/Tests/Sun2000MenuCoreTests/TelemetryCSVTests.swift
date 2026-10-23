import XCTest
@testable import Sun2000MenuCore

final class TelemetryCSVTests: XCTestCase {
    func testLatestSampleUsesNewestTimestampAndHandlesQuotedFields() throws {
        let csv = """
        timestamp_utc,signal,value,unit
        2026-10-23T08:00:00Z,"model_name","SUN2000, inverter",""
        2026-10-23T08:00:00Z,"active_power","500.000000","W"
        2026-10-23T08:01:00Z,"active_power","600.000000","W"
        2026-10-23T08:01:00Z,"serial_number","NS123""456",""
        """

        let values = try TelemetryCSVParser.latestSample(from: csv)

        XCTAssertEqual(values.map(\.signal), ["active_power", "serial_number"])
        XCTAssertEqual(values[0].value, "600.000000")
        XCTAssertEqual(values[1].value, "NS123\"456")
    }

    func testEmptyCSVWithHeaderReturnsNoValues() throws {
        let values = try TelemetryCSVParser.latestSample(
            from: "timestamp_utc,signal,value,unit\n"
        )
        XCTAssertTrue(values.isEmpty)
    }

    func testMalformedCSVHasUsefulError() {
        XCTAssertThrowsError(
            try TelemetryCSVParser.latestSample(
                "timestamp_utc,signal,value,unit\n2026-10-23Z,\"broken"
            )
        )
    }

    func testStatusBarLabelsStayCompact() {
        XCTAssertEqual(TelemetrySignalCatalog.shortLabel(for: "active_power"), "P")
        XCTAssertEqual(TelemetrySignalCatalog.shortLabel(for: "battery_soc"), "SOC")
        XCTAssertEqual(TelemetrySignalCatalog.shortLabel(for: "pv_12_voltage"), "12V")
        XCTAssertLessThanOrEqual(
            TelemetrySignalCatalog.shortLabel(for: "previous_day_statistics_time").count,
            4
        )
    }
}

@MainActor
final class AppSettingsTests: XCTestCase {
    func testSettingsPersistAndDeduplicateSelectedSignals() {
        let suiteName = "sun2000MenuTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defer { defaults.removePersistentDomain(forName: suiteName) }

        let settings = AppSettings(defaults: defaults)
        XCTAssertEqual(settings.port, 502)
        XCTAssertEqual(settings.selectedSignals, ["active_power", "battery_soc"])

        settings.intervalSeconds = 30
        settings.selectedSignals = ["battery_soc", "battery_soc", "active_power"]

        let restored = AppSettings(defaults: defaults)
        XCTAssertEqual(restored.intervalSeconds, 30)
        XCTAssertEqual(restored.selectedSignals, ["battery_soc", "active_power"])
    }
}
