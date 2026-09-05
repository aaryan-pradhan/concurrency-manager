# 18. Metrics and Log Files

Running any of the test harnesses produces several output files (all `.gitignore`d, so they're generated fresh locally and never committed). This document explains what each one is and how to read it.

## `transaction_metrics.txt` — produced by `tests/test_deadlock_detection.cpp`

Written by `writeTransactionMetricsToFile()` (see [The Test Harnesses](17-test-harnesses.md)), **appended to** on every run (not overwritten), so repeated runs accumulate multiple reports in the same file, each with its own timestamp. It has two sections:

### Transaction Details table

One row per logical transaction, with columns:

| Column | Meaning |
|---|---|
| `TxnNum` | The logical transaction number from the test file (e.g., the `1` in `START T1`) |
| `TxnID` | The actual `Transaction` object's ID at the time metrics were last recorded — will differ from `TxnNum` if the transaction restarted with a new ID via `beginTransaction`'s `requestedTxnId` mechanic... though in practice the harness always requests the *same* ID back on restart, so this is normally stable. See [the ConcurrencyManager module](14-module-concurrency-manager.md) for how ID reuse works. |
| `Priority` | The transaction's priority **at its most recent start** — increases by 1 each time it's forced to restart after being aborted by the deadlock detector |
| `Restarts` | How many times this transaction was aborted-and-restarted due to deadlock detection |
| `Locks Acq` / `Locks Fail` | Count of successful vs. failed `acquireLock` calls |
| `Latency(ms)` | Wall-clock time from the transaction's *first* start to its final end (commit or terminal abort) — includes all time spent waiting, being aborted, backing off, and retrying |
| `Status` | `Committed`, `Restarted` (currently mid-retry when the report was generated — this can happen if metrics are written while some thread hasn't finished), or `Aborted` (ended without ever committing) |

### Performance Summary table

Aggregate numbers across the whole run:

| Metric | How it's computed |
|---|---|
| Transactions Completed | Count of transactions that reached `commitTransaction() == true` |
| Transactions Aborted | Explicit/final aborts not caused by deadlock recovery |
| Transactions Restarted | Total count of deadlock-triggered restarts across all transactions |
| Transaction Success Rate | `completed / (completed + restarted + aborted) × 100` |
| Throughput | Successful transactions divided by total test wall-clock duration (transactions per second) |
| Average / Maximum Transaction Latency | Computed only over transactions that actually committed |
| Deadlock Detected | Whether `deadlockDetected` was ever set `true` during the run (i.e., at least one transaction was aborted while waiting on a lock) |

**How to use this file in practice:** if you want to demonstrate that deadlock detection actually works, generate a deadlock-heavy workload (`tests/generate_diff.py`), run `tests/test_deadlock_detection.cpp` against it, and look for `Restarts > 0` on individual rows and `Deadlock Detected: Yes` in the summary — that's direct evidence the background `DeadlockDetector` fired and recovered.

## `rag_log.txt` and `RAGoutput.log` — Resource Allocation Graph snapshots

Written by `ResourceAllocationGraph::logToFile()` (see [the ResourceAllocationGraph module](12-module-resource-allocation-graph.md)), called after essentially every operation by both `2pl_test_runner.cpp` and `test_deadlock_detection.cpp`. Each entry looks like:

```
[2024-01-01 12:00:00] T1 read E: SUCCESS (Line 6)

=== RESOURCE ALLOCATION GRAPH ===
Assignment Edges (R → T):
  R1001 → T1
Request Edges (T → R):
Claim Edges (T → R):
===============================
```

Reading this file sequentially gives you a **complete, step-by-step movie** of the lock state evolving over the entire test run — genuinely useful for manually tracing exactly how a specific deadlock formed, since you can watch the assignment and request edges accumulate right up to the moment a cycle appears. `ResourceAllocationGraph::initLogFile()` writes a header block the first time the file is created; because the file is opened in append mode (`std::ios::app`) on every write, running the same test twice in a row without deleting the file will produce two runs' worth of history stacked in one file.

`RAGoutput.log` is referenced in `2pl_test_runner.cpp`'s `main()` (`ResourceAllocationGraph::initLogFile("RAGoutput.log")`), but `initLogFile` is declared as a regular (non-static) member function on `ResourceAllocationGraph`, not a `static` one — calling it through `ClassName::method(...)` with no object instance, from a plain free function like `main()`, is not valid C++ and won't compile. This is a second, independent compile error in `2pl_test_runner.cpp`, on top of the missing `checkForDeadlocks()` definition covered in [the ConcurrencyManager module](14-module-concurrency-manager.md). Even setting that aside, the actual per-operation logging always targets the hardcoded `rag_log.txt` filename inside `logToFile()` — so in practice you'd only ever see content land in `rag_log.txt`, never `RAGoutput.log`, regardless of which harness you ran. See [Known Issues](20-known-issues-and-inconsistencies.md).

## `*.log` files — the `Logger`-driven text logs

Each `ConcurrencyManager` (and, in `test_lock_manager.cpp`, each standalone `Logger`) is constructed with its own log file path:

| Harness | Log file |
|---|---|
| `src/2pl_test_runner.cpp` | `2pl_test_results.log` |
| `tests/test_deadlock_detection.cpp` | `deadlock_test.log` |
| `tests/test_lock_manager.cpp` | `test_lock_manager.log` |

These contain the timestamped, plain-English narration described in [the Logger module](15-module-logger.md) — lock acquire attempts, grants, releases, waits, transaction lifecycle events, and (from `DeadlockDetector`) cycle-detection debug output and victim-abort warnings. If you want to understand *why* a specific transaction was chosen as a victim, this is the file to grep for its transaction ID (e.g. `grep "T7" deadlock_test.log`) — you'll see its full history of lock attempts leading up to the `"Aborting transaction T7 ... to break deadlock cycle"` line from `DeadlockDetector::RunCycleDetection()`.

## `test_error.txt` — only appears if something throws

`tests/test_deadlock_detection.cpp`'s `main()` catches any exception escaping the whole run and writes its message to this file (since it deliberately produces no console output otherwise). If you don't see the metrics file appear after a run, check here first.

## Cleaning up

All of these are listed in `.gitignore` and removed by `make clean` (which also deletes the compiled binaries) — see [Building and Running](19-building-and-running.md).

Next: [Building and Running](19-building-and-running.md).
