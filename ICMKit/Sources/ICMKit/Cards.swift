// Not yet compiled (written without a Swift toolchain). Thin wrappers over ICMCore.
import ICMCore

/// Card helpers using the ICMCore encoding: rank * 4 + suit, rank 0...12 = 2...A, suit 0...3 = c, d, h, s.
public enum Cards {
    /// Parses a run of cards such as "Ks7d2c" or "Ks 7d 2c".
    public static func parse(_ text: String) -> [UInt8]? {
        var out = [UInt8](repeating: 0, count: 7)
        let n = text.withCString { cards_parse($0, &out, 7) }
        return n < 0 ? nil : Array(out.prefix(Int(n)))
    }

    public static func name(_ card: UInt8) -> String {
        var buf = [CChar](repeating: 0, count: 3)
        card_format(Int32(card), &buf)
        return String(cString: buf)
    }

    /// 1326 combo weights from standard range notation ("22+,ATs+,KQo:0.5").
    public static func range(_ text: String) -> [Float]? {
        var w = [Float](repeating: 0, count: Int(NUM_COMBOS))
        let r = text.withCString { range_parse($0, &w) }
        return r == 0 ? w : nil
    }
}
