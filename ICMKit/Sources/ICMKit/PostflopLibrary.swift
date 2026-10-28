// Not yet compiled (written without a Swift toolchain). Thin wrapper over libreader.
// The library format and reader are Part B2's work; until then `init` throws.
import Foundation
import ICMCore

public struct LibraryValues: Sendable {
    public let betPercent: Float
    public let betSize: Int32            // LR_SIZE_33, LR_SIZE_75, LR_SIZE_ALLIN
    public let checkRaisePercent: Float? // out of position only
    public let stackOffCategory: Int32?
    public let bluffPercent: Float
}

public final class PostflopLibrary: @unchecked Sendable {
    private let data: Data
    private let handle: OpaquePointer

    public init(data: Data) throws {
        self.data = data
        var err = [CChar](repeating: 0, count: 256)
        let h: OpaquePointer? = data.withUnsafeBytes { buf in
            lr_open_memory(buf.baseAddress, buf.count, &err, err.count)
        }
        guard let h else { throw SolverError(message: String(cString: err)) }
        handle = h
    }

    deinit { lr_close(handle) }

    /// - position: LR_IN_POSITION or LR_OUT_OF_POSITION; potType: LR_POT_SINGLE_RAISED or LR_POT_THREE_BET.
    public func query(heroPremium: Double, villainPremium: Double, position: Int32, potType: Int32,
                      spr: Double, flop: [UInt8], icm: Bool) -> LibraryValues? {
        guard flop.count == 3 else { return nil }
        var v = lr_values()
        let r = data.withUnsafeBytes { _ in
            lr_query(handle, heroPremium, villainPremium, position, potType, spr, flop, icm ? 1 : 0, &v)
        }
        guard r == 0 else { return nil }
        return LibraryValues(betPercent: v.bet_pct, betSize: v.bet_size,
                             checkRaisePercent: v.check_raise_pct < 0 ? nil : v.check_raise_pct,
                             stackOffCategory: v.stack_off_category < 0 ? nil : v.stack_off_category,
                             bluffPercent: v.bluff_pct)
    }
}
