# Documentation Index

This is a complete, from-scratch explanation of the **Concurrency Manager** project — a C++ simulation of how database systems keep concurrent transactions from corrupting each other's data, and how they detect and recover from deadlocks.

You are assumed to know **nothing** about this project going in. Each document below explains exactly one idea or one piece of code. Read them in order the first time; after that, use this index to jump straight to what you need.

## Part 1 — Background concepts (no code yet)

These build up the vocabulary and theory you need before any of the code will make sense.

1. [What is this project?](01-introduction.md)
2. [Databases and transactions](02-databases-and-transactions.md)
3. [Threads and race conditions](03-concurrency-threads-and-race-conditions.md)
4. [Locks and concurrency control](04-locks-and-concurrency-control.md)
5. [The Two-Phase Locking (2PL) protocol](05-two-phase-locking-protocol.md)
6. [Deadlocks, explained](06-deadlocks-explained.md)
7. [Resource Allocation Graphs](07-resource-allocation-graphs.md)
8. [Wait-for graphs and victim selection](08-wait-for-graphs-and-victim-selection.md)

## Part 2 — How the system is built

9. [Architecture overview](09-architecture-overview.md) — start here for the code

## Part 3 — One file, one document (the modules)

10. [`Transaction`](10-module-transaction.md) — `include/transaction.h`, `src/transaction.cpp`
11. [`LockManager`](11-module-lock-manager.md) — `include/lock_manager.h`, `src/lock_manager.cpp`
12. [`ResourceAllocationGraph`](12-module-resource-allocation-graph.md) — `include/resource_manager.h`, `src/resource_manager.cpp`
13. [`DeadlockDetector`](13-module-deadlock-detector.md) — `include/deadlock_detector.h`, `src/deadlock_detector.cpp`
14. [`ConcurrencyManager`](14-module-concurrency-manager.md) — `include/concurrency_manager.h`, `src/concurrency_manager.cpp`
15. [`Logger`](15-module-logger.md) — `include/logger.h`, `src/logger.cpp`

## Part 4 — Running it

16. [The test-file mini-language](16-test-file-language.md) — how `.txt` schedules like `tests/test1.txt` work
17. [The test harnesses](17-test-harnesses.md) — the three different `main()` programs in this repo
18. [Metrics and log files](18-metrics-and-log-files.md) — how to read the output the programs produce
19. [Building and running](19-building-and-running.md) — exact commands, what works today

## Part 5 — Housekeeping

20. [Known issues and inconsistencies](20-known-issues-and-inconsistencies.md) — honest notes on where the code, the `README.md`, and reality disagree
21. [Glossary](21-glossary.md) — every term used in this documentation, in one place

---

**Quick orientation, if you only read one paragraph:** this project simulates the part of a database engine that lets many transactions run *at the same time* without corrupting each other's data. It does this by making every transaction acquire a **lock** before touching a piece of data (the **Two-Phase Locking** protocol), and it runs a background thread that watches for **deadlocks** — situations where transactions are stuck waiting for each other in a circle — and kills off the least important transaction in the circle to break it. Everything else in this repo (the logger, the graphs, the test harnesses) exists to implement, observe, or exercise that one idea.
