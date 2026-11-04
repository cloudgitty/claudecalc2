"""Unit tests for the rake rule (stage 2). Run: python3 tests/test_rake.py"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from rake import matched_pot, rake  # noqa: E402

fails = 0


def check(name, got, want):
    global fails
    ok = abs(got - want) < 1e-9
    fails += not ok
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {got} (want {want})")


check("preflop end: no rake", rake(7.5, False), 0.0)
check("flop pot 6.5: 10%", rake(6.5, True), 0.65)
check("pot 49 below cap", rake(49, True), 4.9)
check("pot 50 cap binds", rake(50, True), 5.0)
check("pot 200 capped", rake(200, True), 5.0)
check("preflop all-in is raked (flop dealt)", rake(200.5, True), 5.0)
check("uncalled bet returned", matched_pot(20, 15, 0), 20.0)
check("called bet in pot", matched_pot(20, 15, 15), 50.0)
check("raked pot after uncalled raise", rake(matched_pot(20, 40, 15), True), 5.0)
check("rake 5% sensitivity", rake(40, True, pct=0.05), 2.0)
check("cap 3 sensitivity", rake(40, True, cap=3.0), 3.0)
print("test_rake.py:", "PASS" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
