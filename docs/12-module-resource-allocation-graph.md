# 12. Module: `ResourceAllocationGraph`

**Files:** `include/resource_manager.h`, `src/resource_manager.cpp`

Read [Resource Allocation Graphs](07-resource-allocation-graphs.md) first for the theory this class implements; this document is about the C++ implementation itself.

## Data structures

```cpp
std::unordered_map<int, std::set<int>> assignmentEdges; // resourceId -> set of transaction IDs holding it
std::unordered_map<int, std::set<int>> requestEdges;     // transactionId -> set of resource IDs it's waiting for
std::unordered_map<int, std::set<int>> claimEdges;       // transactionId -> set of resource IDs it might request later (unused in practice)
Logger &logger;
mutable std::recursive_mutex mtx;
```

Each map represents one of the three edge types from [the theory document](07-resource-allocation-graphs.md), stored as an adjacency list (a map from one node to the set of nodes it connects to).

### Why a `recursive_mutex` instead of a plain `mutex`?

A plain `std::mutex` deadlocks the *thread itself* if it tries to lock it twice without unlocking in between (this would be a bug entirely internal to this class — unrelated to the transaction-level deadlocks this whole project studies, but a real risk given how many of these methods call each other). `addAssignmentEdge()`, for instance, calls `removeRequestEdge()` internally:

```cpp
void ResourceAllocationGraph::addAssignmentEdge(int resourceId, int txnId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    assignmentEdges[resourceId].insert(txnId);
    removeRequestEdge(txnId, resourceId); // this ALSO locks mtx
    ...
}
```

Because `mtx` is a `std::recursive_mutex`, the same thread can lock it again inside `removeRequestEdge()` without deadlocking against itself; the lock is only actually released once the outermost `lock_guard` goes out of scope.

## Edge management methods

Each edge type gets a matched add/remove pair, all following the same shape: lock the mutex, insert/erase from the relevant map, clean up the outer map entry if its set becomes empty, log a debug line. See `addAssignmentEdge`, `removeAssignmentEdge`, `addRequestEdge`, `removeRequestEdge`, `addClaimEdge`, `removeClaimEdge`.

`clearTransaction(txnId)` is the "transaction is done" cleanup call — removes every request edge, claim edge, and assignment edge involving that transaction in one pass. `LockManager::releaseAllLocks()` calls this whenever a transaction commits, aborts, or is aborted by the deadlock detector.

## `detectDeadlock()` and `hasCycle()` — the cycle search

```cpp
bool ResourceAllocationGraph::detectDeadlock(std::vector<int>& deadlockCycle) {
    for (const auto& entry : requestEdges) {
        int startTxnId = entry.first;
        std::set<int> visited, recursionStack;
        std::vector<int> path;
        if (hasCycle(startTxnId, path, visited, recursionStack)) {
            deadlockCycle = path;
            return true;
        }
    }
    return false;
}
```

It tries a fresh depth-first search starting from every transaction that has at least one outstanding request (a transaction with no pending requests can't be part of a *waiting* cycle). `hasCycle()` walks: transaction → (its requested resources) → (the resources' current holders) → recurse, watching for a holder that's already on the current search's `recursionStack`.

One implementation detail worth knowing if you read the `path` output: resource IDs are pushed onto `path` as **negative numbers** (`path.push_back(-resourceId)`), purely so that a printed/returned path can distinguish "this entry is a resource" from "this entry is a transaction" without needing a second parallel array — transaction IDs and resource IDs share the same integer type but are always non-negative in this project's convention, so negating one of them is an easy, if slightly unusual, way to tag it.

## `toString()` and `logToFile()`

`toString()` renders the entire graph as human-readable text, grouped by edge type:

```
=== RESOURCE ALLOCATION GRAPH ===
Assignment Edges (R → T):
  R1001 → T1
Request Edges (T → R):
  T2 → R1001
Claim Edges (T → R):
===============================
```

`logToFile()` appends a timestamped snapshot of exactly this text to `rag_log.txt`, optionally preceded by a caller-supplied description of what just happened (e.g., `"T1 read E: SUCCESS (Line 6)"`). This is how the test harnesses in `tests/` produce a readable, step-by-step trace of the whole system's lock state changing over time — see [Metrics and Log Files](18-metrics-and-log-files.md) for how to read the resulting file.

## How it's kept up to date (it never inspects `LockManager` itself)

`ResourceAllocationGraph` has **no reference to `LockManager`** and never queries it. Every edge it has is pushed into it *by* `LockManager`, at the exact moment a lock event happens (see the calls to `rag.addAssignmentEdge(...)` etc. throughout `src/lock_manager.cpp`, covered in [the LockManager module](11-module-lock-manager.md)). This is a deliberate one-way data flow: `LockManager` is the single source of truth for locking decisions, and `ResourceAllocationGraph` is a passive, continuously-updated *view* of that truth, kept solely for graph-based analysis (cycle detection, human-readable logging) rather than being consulted for locking decisions itself.

## Why `detectDeadlock()` here isn't what actually catches deadlocks automatically

As flagged in [Architecture Overview](09-architecture-overview.md), nothing in the current codebase calls `ResourceAllocationGraph::detectDeadlock()` (or `LockManager::detectDeadlock()`, which just forwards to it) on any kind of timer or loop. It's reachable and correct, but unused automatically — the actual, running, periodic deadlock resolution comes from the completely separate `DeadlockDetector` class and its own internal wait-for graph, covered next.

Next: [`DeadlockDetector`](13-module-deadlock-detector.md).
