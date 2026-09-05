# 17. The Test Harnesses

This repository has **three** different `main()`-containing programs, each with a different purpose and, notably, a different level of finish. This document explains what each one does and how they differ from each other.

## 1. `src/2pl_test_runner.cpp` — the simplest runner, currently unbuildable as-is

This is the runner described in the top-level `README.md`. It:

1. Parses a test file (see [The Test-File Mini-Language](16-test-file-language.md)) using regexes that require the **entire line** to match a comment pattern to be skipped (no inline `//` stripping).
2. Spawns one thread per transaction, staggering their start by 50ms each (`std::this_thread::sleep_for(std::chrono::milliseconds(50))` in the launch loop) — a simple way to reduce (but not eliminate) exact-simultaneous-start races, purely for making console output easier to follow.
3. Each transaction thread executes its operations, printing progress to the console via a `SYNCHRONIZED_COUT` macro (a mutex-protected `std::cout`) and also writing a step-by-step trace to the Resource Allocation Graph log (`cm.logResourceAllocationGraph(...)`) after every single operation.
4. After each operation, there's a **10% random chance** (`rand() % 10 == 0`) of calling `cm.checkForDeadlocks()` to check for and resolve a deadlock right then.
5. Prints the final system state (`cm.getSystemState()`) once every thread has finished.

**This program does not currently build.** Step 4 calls `ConcurrencyManager::checkForDeadlocks()`, which is declared in `include/concurrency_manager.h` but has no implementation in `src/concurrency_manager.cpp` — see [the ConcurrencyManager module](14-module-concurrency-manager.md) and [Known Issues](20-known-issues-and-inconsistencies.md). Consistent with this, the `makefile` in this repository does **not** define a target that builds `2pl_test_runner` at all (only `deadlock_test_runner`, described below) — so this gap is already reflected in the build configuration, even though the top-level `README.md`'s instructions still reference it.

## 2. `tests/test_deadlock_detection.cpp` — the actual, working, feature-complete harness

This is the most sophisticated program in the repository, and it's the one the `makefile`'s `run` target actually builds and runs (against `tests/test4.txt`). Differences from `2pl_test_runner.cpp`:

- **All console chatter is stripped.** `SYNCHRONIZED_COUT` is redefined to do nothing (`#define SYNCHRONIZED_COUT(x) {}`) — the file's own top comment says "Modified to remove all console output." Everything interesting is written to log/metrics files instead (see [Metrics and Log Files](18-metrics-and-log-files.md)).
- **Uses a `std::barrier`** (`startBarrier->arrive_and_wait()`) so every transaction thread starts its actual work at the same instant, once all threads have been spawned — a more precise way to maximize real contention than the 50ms staggering used in `2pl_test_runner.cpp`.
- **Tracks detailed per-transaction metrics** (`TransactionMetrics` struct: restart count, lock acquisitions/failures, latency, priority, committed/aborted status) and writes a formatted report to `transaction_metrics.txt` at the end (`writeTransactionMetricsToFile`).
- **Implements the full retry-with-backoff loop** described in [Wait-for Graphs and Victim Selection](08-wait-for-graphs-and-victim-selection.md): if a transaction's lock request comes back with the transaction now in `ABORTED` state (meaning the background `DeadlockDetector` chose it as a victim while it was waiting), the thread doesn't exit — it increments a local `priority` variable, calls `cm.beginTransaction(..., txnId, priority)` to restart with the *same* transaction number but a fresh `Transaction` object and a higher priority, sleeps for a calculated exponential backoff, and resets its operation index back to the start (`i = -1`, then the loop's `i++` brings it to `0`).
- **Configures a faster detection interval**: `ConcurrencyManager cm("deadlock_test.log", 100);` — 100ms instead of `ConcurrencyManager`'s own default of 200ms, so the deadlock detector reacts more quickly during these (typically shorter, denser) test runs.
- **Parses the test-file language slightly more permissively** than `2pl_test_runner.cpp`: it strips inline `//` comments from a line that has other content before them, and trims leading/trailing whitespace, before matching against the command regexes.

## 3. `tests/test_lock_manager.cpp` — a focused, standalone unit test, also currently unbuildable as-is

This program doesn't use `ConcurrencyManager` at all — it constructs a bare `LockManager` directly and exercises it with a series of hand-written scenarios (basic acquire/release, compatibility rules, upgrades, information-retrieval queries), printing PASS/FAIL results to the console via `printTestResult()`.

**This program also does not currently build against the rest of the codebase as it stands today.** It constructs its `LockManager` with a single argument:

```cpp
Logger logger("test_lock_manager.log", true, LogLevel::DEBUG);
LockManager lockManager(logger);
```

but the current `LockManager` constructor requires **two** arguments — a `Logger&` and a `ResourceAllocationGraph&` (`explicit LockManager(Logger &logger, ResourceAllocationGraph &rag);`, added when the Resource Allocation Graph feature was integrated into `LockManager`). This test file predates that change and was never updated to match. See [Known Issues](20-known-issues-and-inconsistencies.md).

## `tests/test_tree_protocol.cpp` — empty

This file exists but currently has **zero lines of content**. Combined with the "tree protocol" logging methods present on `Logger` (`logTreeLockRequest`, `logTreeLockViolation` — see [the Logger module](15-module-logger.md)), this strongly suggests a planned-but-never-written test for the **tree locking protocol** (an alternative concurrency-control scheme for hierarchical data, distinct from 2PL) — scaffolding for a feature that was never implemented.

## Summary table

| File | Uses `ConcurrencyManager`? | Builds today? | Purpose |
|---|:---:|:---:|---|
| `src/2pl_test_runner.cpp` | Yes | ❌ No (`checkForDeadlocks` undefined) | Simple, console-visible schedule runner |
| `tests/test_deadlock_detection.cpp` | Yes | ✅ Yes | Full-featured, metrics-producing, retry-capable harness — the "real" one |
| `tests/test_lock_manager.cpp` | No (bare `LockManager`) | ❌ No (stale constructor call) | Focused unit tests of lock compatibility/upgrade logic |
| `tests/test_tree_protocol.cpp` | — | — (empty file) | Unimplemented placeholder |

Next: [Metrics and Log Files](18-metrics-and-log-files.md) — how to read the output the working harness produces.
