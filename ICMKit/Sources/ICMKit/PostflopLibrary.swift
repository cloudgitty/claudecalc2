// Not yet compiled (written without a Swift toolchain). Thin wrapper over libreader.
// File format: LIBRARY_FORMAT.md at the repo root.
import Foundation
import ICMCore

public enum PostflopPosition: Int32, Sendable { case inPosition = 0, outOfPosition = 1 }
public enum PostflopPotType: Int32, Sendable { case singleRaised = 0, threeBet = 1 }
public enum PostflopBetSize: Int32, Sendable { case third = 0, threeQuarters = 1, allIn = 2 }

/// Spec hand categories, strongest first (handcat.h).
public enum PostflopHandCategory: Int32, Sendable {
    case twoPairOrBetter = 0, topPair, middleOrBottomPair, strongDraw, weakDraw, air
}

public struct LibraryValues: Sendable {
    public let betPercent: Float
    public let betSize: PostflopBetSize?            // nil when hero never bets
    public let checkRaisePercent: Float?            // out of position only
    public let stackOffCategory: PostflopHandCategory?
    public let bluffPercent: Float
    public let isFallback: Bool                     // true when the nearest solved spot was used
}

public final class PostflopLibrary: @unchecked Sendable {
    private let handle: OpaquePointer

    /// Loads LiteLibrary.bin (for example from the app bundle). Throws if the file is invalid.
    public init(data: Data) throws {
        var err = [CChar](repeating: 0, count: 256)
        let h: OpaquePointer? = data.withUnsafeBytes { buf in
            lr_open_memory(buf.baseAddress, buf.count, &err, err.count)
        }
        guard let h else { throw SolverError(message: String(cString: err)) }
        handle = h
    }

    deinit { lr_close(handle) }

    public var entryCount: Int { Int(lr_entry_count(handle)) }

    /// Tiers 1...5 that are fully solved in this file.
    public var completeTiers: [Int] {
        let bits = lr_tiers_complete(handle)
        return (1...5).filter { bits & (1 << UInt32($0 - 1)) != 0 }
    }

    /// Values for any spot. Premiums are fractions (0.05 = 5 points, see CoreICM.riskPremium);
    /// `flop` holds three cards (Cards.parse); `icm: false` gives the chip-EV baseline.
    public func query(heroPremium: Double, villainPremium: Double, position: PostflopPosition,
                      potType: PostflopPotType, spr: Double, flop: [UInt8], icm: Bool) -> LibraryValues? {
        guard flop.count == 3 else { return nil }
        var v = lr_values()
        let r = flop.withUnsafeBufferPointer { f in
            lr_query(handle, heroPremium, villainPremium, position.rawValue, potType.rawValue, spr,
                     f.baseAddress, icm ? 1 : 0, &v)
        }
        guard r == 0 else { return nil }
        return LibraryValues(betPercent: v.bet_pct,
                             betSize: PostflopBetSize(rawValue: v.bet_size),
                             checkRaisePercent: v.check_raise_pct < 0 ? nil : v.check_raise_pct,
                             stackOffCategory: PostflopHandCategory(rawValue: v.stack_off_category),
                             bluffPercent: v.bluff_pct,
                             isFallback: v.source == LR_SOURCE_NEAREST)
    }
}
