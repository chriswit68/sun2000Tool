// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "sun2000Menu",
    platforms: [.macOS(.v15)],
    products: [
        .executable(name: "sun2000Menu", targets: ["sun2000Menu"])
    ],
    targets: [
        .target(name: "Sun2000MenuCore"),
        .executableTarget(
            name: "sun2000Menu",
            dependencies: ["Sun2000MenuCore"]
        ),
        .testTarget(
            name: "Sun2000MenuCoreTests",
            dependencies: ["Sun2000MenuCore"]
        )
    ],
    swiftLanguageModes: [.v5]
)
