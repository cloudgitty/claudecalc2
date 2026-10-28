// swift-tools-version:6.0
// Not yet compiled: written on a Linux machine without Swift. See ICMKit/PROGRESS.md.
import PackageDescription

let package = Package(
    name: "ICMKit",
    platforms: [.iOS(.v18), .macOS(.v14)],
    products: [
        .library(name: "ICMKit", targets: ["ICMKit"]),
    ],
    targets: [
        .target(
            name: "ICMCore",
            path: "Sources/ICMCore",
            publicHeadersPath: "include",
            cSettings: [
                .define("_POSIX_C_SOURCE", to: "200809L"),
                .unsafeFlags(["-O3"], .when(configuration: .release)),
            ]
        ),
        .target(
            name: "ICMKit",
            dependencies: ["ICMCore"],
            path: "Sources/ICMKit"
        ),
    ],
    cLanguageStandard: .c11
)
