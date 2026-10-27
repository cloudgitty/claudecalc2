# PROGRESS

- 2026-10-26 23:22:23 (1) class list: 962988 classes, 20358520 hands verified; evaluator self-test ok (9s)
- 2026-10-26 23:26:01 (2a) throughput 12.0M trials/s on 4 threads => one band at 4000 boards for all 962988 classes = 5.3 min
- Budget plan (from measured 12M trials/s): simulate ALL 962,988 classes (census, > the 80k minimum) x 12 reference bands x 4,000 boards = 4.6e10 trials ~ 64 min wall. Borderline re-sim budget <= 60 min wall (SE <= 0.3 pts needs ~25k trials/class/range ~ 480 class-ranges/s). Total simulation budget 3 h wall / 12 CPU-h; stop-loss order per spec section 10 if exceeded.
- 2026-10-26 23:30:16 (4) R0 calibrated: srp,oop=0.309, srp,ip=1.653, 3bp,oop=0.992, 3bp,ip=1.095, 4bp,oop=0.994, 4bp,ip=1.004, srp,sbcold=0.271
- 2026-10-26 23:34:14 (2b) band 0 [0-1.5%] simulated: 962988 classes x 4000 boards in 5.8 min
- 2026-10-26 23:34:46 (4) R0 calibrated: srp,oop=0.810, srp,ip=1.177, 3bp,oop=0.856, 3bp,ip=1.157, 4bp,oop=0.826, 4bp,ip=1.175, srp,sbcold=0.711
- 2026-10-26 23:40:53 (2b) band 1 [1.5-3%] simulated: 962988 classes x 4000 boards in 6.6 min
- 2026-10-26 23:45:54 (2b) band 2 [3-5%] simulated: 962988 classes x 4000 boards in 5.0 min
- 2026-10-26 23:50:40 (2b) band 3 [5-8%] simulated: 962988 classes x 4000 boards in 4.8 min
- 2026-10-26 23:55:27 (2b) band 4 [8-12%] simulated: 962988 classes x 4000 boards in 4.8 min
- 2026-10-27 00:00:12 (2b) band 5 [12-17%] simulated: 962988 classes x 4000 boards in 4.7 min
- 2026-10-27 00:04:59 (2b) band 6 [17-23%] simulated: 962988 classes x 4000 boards in 4.8 min
