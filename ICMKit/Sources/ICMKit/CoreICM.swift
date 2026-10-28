// Not yet compiled (written without a Swift toolchain). Thin wrappers over ICMCore.
import ICMCore

/// The C Malmuth-Harville ICM and risk premium. Results must match the app's Swift `ICMEngine`.
public enum CoreICM {
    /// Prize equity for each stack (up to 9 players). Payouts list 1st, 2nd, ...
    public static func equity(stacks: [Double], payouts: [Double]) -> [Double]? {
        guard (1...9).contains(stacks.count) else { return nil }
        var eq = [Double](repeating: 0, count: stacks.count)
        let r = icm_equity(Int32(stacks.count), stacks, Int32(payouts.count), payouts, &eq)
        return r == 0 ? eq : nil
    }

    /// Extra ICM equity hero needs, on top of 50%, to call villain's all-in (0.05 = 5 points).
    public static func riskPremium(stacks: [Double], payouts: [Double], hero: Int, villain: Int) -> Double? {
        var p = 0.0
        let r = risk_premium(Int32(stacks.count), stacks, Int32(payouts.count), payouts, Int32(hero), Int32(villain), &p)
        return r == 0 ? p : nil
    }
}
