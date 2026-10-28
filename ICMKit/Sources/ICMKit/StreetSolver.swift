// Not yet compiled (written without a Swift toolchain). Thin wrapper over the C street solver.
import ICMCore

public enum PayoffMode: Sendable {
    case chips
    /// Every player's chips behind at the start of the spot, the payouts, and the
    /// seats of the out-of-position and in-position players.
    case icm(stacks: [Double], payouts: [Double], oopSeat: Int, ipSeat: Int)
}

public struct SolverSpot: Sendable {
    public var board: [UInt8]          // 3, 4 or 5 cards (Cards.parse)
    public var oopRange: [Float]       // 1326 weights (Cards.range)
    public var ipRange: [Float]
    public var pot: Double
    public var effectiveStack: Double
    public var mode: PayoffMode = .chips
    public var maxIterations: Int = 1000
    public var targetExploitabilityPercent: Double = 0.5
    public var threads: Int = 2

    public init(board: [UInt8], oopRange: [Float], ipRange: [Float], pot: Double, effectiveStack: Double) {
        self.board = board
        self.oopRange = oopRange
        self.ipRange = ipRange
        self.pot = pot
        self.effectiveStack = effectiveStack
    }
}

public struct SolverResult: Sendable {
    public let iterations: Int
    public let exploitabilityPercent: (oop: Double, ip: Double)
    public let value: (oop: Double, ip: Double)
    public let seconds: Double
    public let peakMemoryBytes: Int
    public let cancelled: Bool
}

public struct SolverError: Error, Sendable { public let message: String }

/// Solves a turn or river spot (flop works too but is meant for the cloud library build).
/// Create and solve on a background thread; `cancel()` and `progress` may be read from any thread.
public final class StreetSolver: @unchecked Sendable {
    private let handle: OpaquePointer
    private var progressBox: Unmanaged<ProgressBox>?

    private final class ProgressBox {
        let callback: (Int, Double) -> Void
        init(_ callback: @escaping (Int, Double) -> Void) { self.callback = callback }
    }

    /// `onProgress(iteration, exploitabilityPercent)` is called on the solving thread.
    public init(spot: SolverSpot, onProgress: ((Int, Double) -> Void)? = nil) throws {
        guard spot.oopRange.count == Int(NUM_COMBOS), spot.ipRange.count == Int(NUM_COMBOS),
              (3...5).contains(spot.board.count) else { throw SolverError(message: "bad spot") }
        var c = ss_config()
        ss_config_default(&c)
        withUnsafeMutableBytes(of: &c.board) { buf in
            for (i, card) in spot.board.enumerated() { buf[i] = card }
        }
        c.board_len = Int32(spot.board.count)
        c.pot = spot.pot
        c.stack = spot.effectiveStack
        c.max_iterations = Int32(spot.maxIterations)
        c.target_exploitability_pct = spot.targetExploitabilityPercent
        c.threads = Int32(max(1, spot.threads))
        if case let .icm(stacks, payouts, oop, ip) = spot.mode {
            guard stacks.count <= Int(SS_MAX_PLAYERS), payouts.count <= Int(SS_MAX_PLAYERS) else {
                throw SolverError(message: "too many players")
            }
            c.mode = Int32(SS_MODE_ICM)
            c.icm_players = Int32(stacks.count)
            c.icm_num_payouts = Int32(payouts.count)
            withUnsafeMutableBytes(of: &c.icm_stacks) { buf in
                for (i, s) in stacks.enumerated() { buf.storeBytes(of: s, toByteOffset: i * 8, as: Double.self) }
            }
            withUnsafeMutableBytes(of: &c.icm_payouts) { buf in
                for (i, p) in payouts.enumerated() { buf.storeBytes(of: p, toByteOffset: i * 8, as: Double.self) }
            }
            c.seat = (Int32(oop), Int32(ip))
        }
        if let onProgress {
            let box = Unmanaged.passRetained(ProgressBox(onProgress))
            progressBox = box
            c.progress_user = box.toOpaque()
            c.progress = { user, iteration, expl in
                guard let user else { return }
                Unmanaged<ProgressBox>.fromOpaque(user).takeUnretainedValue().callback(Int(iteration), expl)
            }
        }
        var err = [CChar](repeating: 0, count: 256)
        let made: OpaquePointer? = spot.oopRange.withUnsafeBufferPointer { o in
            spot.ipRange.withUnsafeBufferPointer { i in
                c.range = (o.baseAddress, i.baseAddress)
                return ss_create(&c, &err, err.count)
            }
        }
        guard let made else {
            progressBox?.release()
            throw SolverError(message: String(cString: err))
        }
        handle = made
    }

    deinit {
        ss_destroy(handle)
        progressBox?.release()
    }

    /// Runs to the stop rule (blocking). Call from a background thread.
    public func solve() -> SolverResult {
        var r = ss_result()
        let status = ss_solve(handle, &r)
        return SolverResult(iterations: Int(r.iterations),
                            exploitabilityPercent: (r.exploitability_pct.0, r.exploitability_pct.1),
                            value: (r.value.0, r.value.1),
                            seconds: r.seconds,
                            peakMemoryBytes: Int(r.peak_memory),
                            cancelled: status == SS_CANCELLED)
    }

    /// Thread-safe: stops `solve()` after the current iteration.
    public func cancel() { ss_cancel(handle) }

    public var iterationsDone: Int { Int(ss_progress_iterations(handle)) }

    public var rootNode: Int32 { ss_root(handle) }

    public func children(of node: Int32) -> [Int32] {
        let n = ss_node_num_children(handle, node)
        return n <= 0 ? [] : (0..<n).map { ss_node_child(handle, node, $0) }
    }

    /// (action, chips put in on this street) for the edge leading into `node`.
    public func action(into node: Int32) -> (kind: Int32, amount: Double) {
        (ss_node_action(handle, node), ss_node_amount(handle, node))
    }

    /// Average strategy at a decision node: result[action][combo] for 1326 combos.
    public func strategy(at node: Int32) -> [[Float]] {
        let k = Int(ss_node_num_children(handle, node))
        guard k > 0 else { return [] }
        var flat = [Float](repeating: 0, count: k * Int(NUM_COMBOS))
        guard ss_node_strategy(handle, node, &flat) == k else { return [] }
        let n = Int(NUM_COMBOS)
        return (0..<k).map { Array(flat[($0 * n)..<(($0 + 1) * n)]) }
    }

    /// Child of a chance node for a dealt card (isomorphic cards share a child).
    public func child(of chanceNode: Int32, card: UInt8) -> Int32? {
        let c = ss_chance_child(handle, chanceNode, Int32(card), nil)
        return c < 0 ? nil : c
    }
}
