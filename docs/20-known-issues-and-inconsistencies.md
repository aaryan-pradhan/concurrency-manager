# 20. Known Issues and Inconsistencies

This documentation set is meant to describe the codebase *as it actually is*, not as it's described elsewhere or as it was originally intended. While writing it, several concrete discrepancies were found between the top-level `README.md`, the code, and what actually builds and runs. They're consolidated here rather than only mentioned in passing, so nothing gets lost.

## 1. `ConcurrencyManager::checkForDeadlocks()` is declared but never defined

- **Where:** declared in `include/concurrency_manager.h`, called from `src/2pl_test_runner.cpp`, never defined in `src/concurrency_manager.cpp` (or anywhere else).
- **Effect:** any build that links `2pl_test_runner.cpp` against the current `concurrency_manager.cpp` fails at the link step with an undefined-reference error.
- **Consistent evidence:** the `makefile` doesn't define a build target for `2pl_test_runner` at all — only for the working `deadlock_test_runner`. Whoever last touched the build system appears to already be aware `2pl_test_runner` doesn't currently build.
- See: [the ConcurrencyManager module](14-module-concurrency-manager.md), [Building and Running](19-building-and-running.md).

## 2. `2pl_test_runner.cpp` also calls a non-static member function incorrectly

- **Where:** `ResourceAllocationGraph::initLogFile("RAGoutput.log")` is called in `main()`, using class-scope syntax with no object instance — but `initLogFile` is a regular (non-static) member function.
- **Effect:** a second, independent reason this file won't compile, on top of issue #1.
- See: [Metrics and Log Files](18-metrics-and-log-files.md).

## 3. `tests/test_lock_manager.cpp` uses an outdated `LockManager` constructor signature

- **Where:** `LockManager lockManager(logger);` — a single-argument call.
- **Current reality:** `LockManager`'s constructor is `explicit LockManager(Logger &logger, ResourceAllocationGraph &rag);` — it requires a `ResourceAllocationGraph` reference too, added when the RAG feature was integrated into `LockManager`.
- **Effect:** this file doesn't compile against the current headers. It appears to predate the RAG integration and was never updated afterward.
- See: [The Test Harnesses](17-test-harnesses.md).

## 4. The top-level `README.md` describes an older, simpler version of the system

The `README.md`'s "Overview" section says the project provides "Deadlock prevention using timeouts" and doesn't mention the `DeadlockDetector`, `ResourceAllocationGraph`, victim selection, priorities, or the metrics/backoff/restart machinery at all. Its "Test Cases" section references `test2.txt` and `test3.txt`, which don't exist in this repository (only `test1.txt`, `test4.txt`, `test5.txt`, `test6.txt`, `dead_test.txt`, and generator-produced files do). This strongly suggests the `README.md` documents an earlier stage of the project, before deadlock **detection** (as opposed to simple timeout-based prevention) was added. Per an explicit decision when this `docs/` directory was created, `README.md` has been left untouched rather than rewritten — treat this documentation set (starting from [the index](README.md)) as the current, authoritative description of the system, and the top-level `README.md` as a historical artifact.

## 5. The `LockManager`'s declared 5-second lock timeout is never applied

- **Where:** `include/lock_manager.h` declares `const std::chrono::milliseconds lockTimeout{5000};`, matching the `README.md`'s claim of a "5-second timeout prevents indefinite waiting."
- **Current reality:** `LockManager::waitForLock()` calls the indefinite-wait overload, `cv.wait(lock, canAcquireLock)`, never `cv.wait_for(...)`. `lockTimeout` is never referenced anywhere in `lock_manager.cpp`.
- **Effect:** a transaction waiting for a lock will wait forever unless the lock becomes available or it gets aborted by the background `DeadlockDetector`. In the currently-working `deadlock_test_runner` harness this is masked entirely by the fact that the deadlock detector *is* running and *does* eventually break every genuine deadlock — but if you ran `LockManager` on its own (e.g., in a fixed version of `test_lock_manager.cpp`) with two transactions permanently deadlocked and no detector attached, both would hang forever with no timeout to save them.
- See: [the LockManager module](11-module-lock-manager.md).

## 6. Two separate, non-communicating deadlock-tracking mechanisms exist side by side

`ResourceAllocationGraph::detectDeadlock()` (and `LockManager::detectDeadlock()`, which forwards to it) is fully implemented and correct, but nothing in the current codebase calls it on any automatic schedule — it's reachable only if some caller invokes it manually (which, per issue #1, was the intended role of the now-broken `2pl_test_runner.cpp`'s periodic `checkForDeadlocks()` call). The actual, currently-working automatic deadlock detection and recovery comes entirely from the separate `DeadlockDetector` class and its own independently-maintained wait-for graph. The two graphs represent overlapping information but neither is derived from or checked against the other.
- See: [Resource Allocation Graphs](07-resource-allocation-graphs.md), [Wait-for Graphs and Victim Selection](08-wait-for-graphs-and-victim-selection.md), [Architecture Overview](09-architecture-overview.md).

## 7. Claim edges exist in the data model but are never populated

`ResourceAllocationGraph::addClaimEdge()` / `removeClaimEdge()` are implemented and would work correctly if called, but nothing in the codebase ever calls them. This is scaffolding for deadlock-**avoidance** style algorithms (like the Banker's Algorithm) that need transactions to pre-declare their future resource needs — a feature that was never built on top of this scaffold.
- See: [Resource Allocation Graphs](07-resource-allocation-graphs.md), [Deadlocks, Explained](06-deadlocks-explained.md).

## 8. `tests/test_tree_protocol.cpp` is an empty file

Zero lines of content. Combined with `Logger`'s `logTreeLockRequest` / `logTreeLockViolation` methods, this suggests a planned test for the tree locking protocol (an alternative to 2PL) that was never written.
- See: [The Test Harnesses](17-test-harnesses.md), [the Logger module](15-module-logger.md).

## 9. `DeadlockDetector::BuildWaitForGraph()` scans a hardcoded resource ID range (0–9999)

Rather than asking `LockManager` which resource IDs are actually in use, it loops `for (int resource_id = 0; resource_id < 10000; resource_id++)`. This works for every test harness in this repository (all of which assign small, sequential resource IDs), but it is both a hidden ceiling (a resource ID ≥ 10000 would silently never be checked for deadlock involvement) and a performance inefficiency (scanning 10,000 IDs every single detection cycle — every 100–200ms — regardless of how few resources are actually in use).
- See: [the DeadlockDetector module](13-module-deadlock-detector.md).

## 10. The `Logger`'s structured event-logging API is mostly unused in favor of generic calls

`Logger` provides purpose-built methods like `logLockAcquired()`, `logTransactionAbort()`, etc., but most call sites in `LockManager` and `ConcurrencyManager` instead build ad-hoc strings and call the generic `info()` / `warning()` methods. This is a stylistic inconsistency, not a functional bug — both paths produce log output correctly.
- See: [the Logger module](15-module-logger.md).

## 11. `dontreadme.md` is an informal AI-generated explanation of RAG edge types

The file `dontreadme.md` at the repository root appears to be a saved response from an AI assistant explaining Resource Allocation Graph edge types (it literally begins "Sure! Here's a clean and structured **Markdown** version of the explanation you provided" and ends "Let me know if you'd like this turned into a table or visual flow!" — a leftover conversational preamble/postamble). Its technical content is accurate and consistent with the codebase and with [Resource Allocation Graphs](07-resource-allocation-graphs.md) in this documentation set, but it is informal scratch notes rather than intentional project documentation.

---

None of the issues above prevent the core, working path — `make run` building and executing `deadlock_test_runner` against a test file — from functioning correctly and demonstrating real 2PL locking, priority-based deadlock detection, and victim selection with restart/backoff, exactly as described in [Architecture Overview](09-architecture-overview.md) onward.
