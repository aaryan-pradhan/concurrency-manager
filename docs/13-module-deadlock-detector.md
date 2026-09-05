# 13. Module: `DeadlockDetector`

**Files:** `include/deadlock_detector.h`, `src/deadlock_detector.cpp`

Read [Wait-for Graphs and Victim Selection](08-wait-for-graphs-and-victim-selection.md) first for the theory; this is the code walkthrough.

## What it owns

```cpp
LockManager& lock_manager_;    // reference - queries this to build the wait-for graph
Logger& logger_;
std::unordered_map<txn_id_t, std::vector<txn_id_t>> wait_for_graph_; // adjacency list: T -> [transactions it waits for]
std::mutex graph_mutex_;       // protects wait_for_graph_
std::thread detection_thread_;
std::atomic<bool> running_;
std::condition_variable cv_;
std::mutex cv_mutex_;
uint64_t detection_interval_ms_;
```

It does not own the `LockManager` — it only holds a reference to one that already exists (constructed by `ConcurrencyManager`), and reads from it.

## Lifecycle: the background thread

```cpp
DeadlockDetector::DeadlockDetector(LockManager& lm, Logger& logger, uint64_t interval_ms)
    : lock_manager_(lm), logger_(logger), detection_interval_ms_(interval_ms), running_(true) {
    detection_thread_ = std::thread(&DeadlockDetector::DetectionThread, this);
}
```

The constructor immediately spawns a dedicated OS thread running `DetectionThread()` — a background loop, entirely separate from every transaction's own thread — for as long as this `DeadlockDetector` object exists:

```cpp
void DeadlockDetector::DetectionThread() {
    while (running_) {
        RunCycleDetection();
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(detection_interval_ms_), [this]() { return !running_; });
    }
}
```

The `cv_.wait_for(...)` call here is the "sleep for `detection_interval_ms`, but wake up early and exit immediately if `running_` becomes false" pattern — this makes shutdown (below) fast rather than forcing it to wait out a full sleep interval.

The destructor stops this thread cleanly:

```cpp
DeadlockDetector::~DeadlockDetector() {
    running_ = false;
    cv_.notify_one(); // wake the sleeping thread immediately, rather than waiting for it to time out on its own
    detection_thread_.join(); // block until it has actually finished exiting
}
```

`ConcurrencyManager`'s destructor explicitly calls `deadlockDetector.reset()` *before* touching anything else, precisely so this shutdown sequence runs (and the background thread stops touching `lock_manager_`) before the rest of `ConcurrencyManager`'s state starts being torn down.

## `BuildWaitForGraph()`

```cpp
for (int resource_id = 0; resource_id < 10000; resource_id++) {
    auto waiters = lock_manager_.getWaitingTransactions(resource_id);
    if (waiters.empty()) continue;
    auto holders = lock_manager_.getLockHolders(resource_id);
    for (auto waiter_id : waiters) {
        // skip if waiter itself is aborted
        for (auto holder_id : holders) {
            // skip self-waits and aborted holders
            AddEdge(waiter_id, holder_id); // waiter -> holder
        }
    }
}
```

This iterates a **hardcoded range of resource IDs (0–9999)** rather than asking the `LockManager` which resource IDs are actually in use. It works correctly for this project because every test harness assigns resource IDs starting from small numbers (`1001` upward, or from a small counter starting at `1`) — but it is a real scalability/correctness limitation worth knowing about; see [Known Issues](20-known-issues-and-inconsistencies.md).

The graph is fully rebuilt from an empty map every call (`wait_for_graph_.clear()` at the top) — there's no incremental updating, so the graph is always exactly consistent with the `LockManager`'s state *at the moment of the scan* (which may already be stale by the time the DFS runs a moment later, given other threads keep running concurrently — an inherent, accepted property of periodic detection).

## `HasCycle()` and `DFS()` — with the priority tie-breaking woven in

The interesting design choice here is that **victim selection happens during the same traversal as cycle detection**, not as a separate pass afterward. As `DFS()` unwinds back up the recursion after finding a cycle, at every level it compares the current node's transaction priority against the best (lowest) priority found so far in this cycle, and keeps the lower one:

```cpp
bool DeadlockDetector::DFS(txn_id_t node, ..., txn_id_t& min_priority_txn) {
    visited[node] = true;
    in_stack[node] = true;
    for (const auto& neighbor : wait_for_graph_[node]) {
        if (!visited[neighbor]) {
            if (DFS(neighbor, ..., min_priority_txn)) {
                // update min_priority_txn if `node`'s priority is lower
                return true; // propagate "cycle found" back up the call stack
            }
        } else if (in_stack[neighbor]) {
            // found the cycle right here; seed min_priority_txn with `node`
            return true;
        }
    }
    in_stack[node] = false; // backtrack
    return false;
}
```

`HasCycle()` wraps this by trying every unvisited node as a DFS root (in sorted order, for deterministic behavior across runs), and additionally compares cycles found from *different* roots against each other (in case the graph has multiple disjoint cycles at once — see below).

## `RunCycleDetection()` — detect, abort, repeat

```cpp
void DeadlockDetector::RunCycleDetection() {
    BuildWaitForGraph();
    while (true) {
        txn_id_t victim;
        if (!HasCycle(victim)) break; // no more cycles - done for this pass
        Transaction* txn = Transaction::GetTransaction(victim);
        if (txn) {
            txn->abort();
            lock_manager_.releaseAllLocks(victim);
            wait_for_graph_.erase(victim);              // remove as a source node
            for (auto& [_, edges] : wait_for_graph_)     // strip incoming edges too
                edges.erase(std::remove(edges.begin(), edges.end(), victim), edges.end());
        }
    }
}
```

Looping until `HasCycle()` returns false handles the case of **multiple simultaneous deadlock cycles** in one detection pass — each iteration resolves one cycle and then re-checks the (now smaller) graph from scratch, rather than assuming there's ever only one cycle to worry about.

## Public surface used by the rest of the system

| Method | Used by |
|---|---|
| `AddEdge` / `RemoveEdge` | Internal, called from `BuildWaitForGraph` |
| `HasCycle` | Internal, called from `RunCycleDetection` |
| `GetEdgeList()` | `ConcurrencyManager::getSystemState()`, to print the current wait-for graph for debugging |
| `RunCycleDetection()` | Called automatically by the background thread; not normally called manually |

Next: [`ConcurrencyManager`](14-module-concurrency-manager.md) — the class that ties `Transaction`, `LockManager`, `ResourceAllocationGraph`, `DeadlockDetector`, and `Logger` together into the single API test harnesses actually use.
