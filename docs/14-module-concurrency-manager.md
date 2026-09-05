# 14. Module: `ConcurrencyManager`

**Files:** `include/concurrency_manager.h`, `src/concurrency_manager.cpp`

This is the **facade** of the whole project — the one class every test harness actually talks to. It doesn't implement locking or deadlock detection itself; it owns the classes that do (`LockManager`, `ResourceAllocationGraph`, `DeadlockDetector`), enforces the 2PL phase rules, and manages the collection of currently-active `Transaction` objects. If you've read [Architecture Overview](09-architecture-overview.md), this document fills in the details behind that diagram.

## Construction and destruction

```cpp
ConcurrencyManager::ConcurrencyManager(const std::string& logFilePath, uint64_t detectionIntervalMs)
    : logger(logFilePath, false),      // console output off by default
      rag(logger),
      lockManager(logger, rag),
      nextTxnId(1) {
    deadlockDetector = std::make_unique<DeadlockDetector>(lockManager, logger, detectionIntervalMs);
}
```

The order here matters and is fixed by C++ rules (members are initialized in declaration order, not the order written in the initializer list) — `logger` must exist before `rag` can reference it, `rag` must exist before `lockManager` can reference it, and both must exist before the constructor body can hand them to a freshly-created `DeadlockDetector`.

```cpp
ConcurrencyManager::~ConcurrencyManager() {
    deadlockDetector.reset();          // stop the background thread FIRST
    for (int id : /* every remaining active transaction */) {
        abortTransaction(id, "System shutdown");
    }
}
```

Destroying the `deadlockDetector` first (rather than last) matters: it stops the background thread that reads from `lockManager` and looks up transactions, before those things start disappearing out from under it during the rest of teardown.

## `beginTransaction()` — including the restart case

```cpp
int beginTransaction(const std::string& metadata = "", int requestedTxnId = -1, int priority = 1);
```

Most calls simply pass `requestedTxnId = -1`, meaning "give me a fresh ID" (`nextTxnId++`). But the method also supports **reusing an existing transaction ID** if that transaction has already been aborted:

```cpp
if (requestedTxnId != -1 && transactions.find(txnId) != transactions.end()) {
    auto* txn = getTransaction(txnId);
    if (txn && txn->getState() == TransactionState::ABORTED) {
        transactions.erase(txnId);   // clear out the old, dead one
        // fall through and construct a brand-new Transaction with the same ID
    } else {
        return -1; // refuse: can't restart something that's still active or already committed
    }
}
```

This is exactly what powers the retry-after-deadlock-abort pattern used in `tests/test_deadlock_detection.cpp`: when a transaction is aborted by the `DeadlockDetector`, the test harness calls `beginTransaction(..., txnId, priority)` again with the *same* transaction number, getting a fresh `Transaction` object with a bumped-up priority but a familiar identity. See [Wait-for Graphs and Victim Selection](08-wait-for-graphs-and-victim-selection.md) for why the priority bump matters, and [The Test Harnesses](17-test-harnesses.md) for the full retry loop.

## `acquireLock()` — where the 2PL rule is actually enforced

```cpp
bool ConcurrencyManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait) {
    Transaction* txn = getTransaction(txnId);
    if (!txn) return false;
    if (txn->getState() != TransactionState::GROWING) return false; // <-- the 2PL rule, enforced here

    bool acquired = lockManager.acquireLock(txnId, resourceId, lockType, wait);
    if (acquired) txn->acquireLock(resourceId); // keep Transaction's own bookkeeping in sync
    return acquired;
}
```

This is the single place in the codebase that checks "is this transaction even allowed to be asking for more locks right now?" before delegating the actual grant/deny/wait decision down to `LockManager`. See [The Two-Phase Locking Protocol](05-two-phase-locking-protocol.md) for why this check exists.

## `commitTransaction()` and `abortTransaction()` — the shared shutdown pattern

Both methods follow the same three steps, in the same order, deliberately:

1. Update the `Transaction` object's own state (`txn->commit()` or `txn->abort()`).
2. Release **every** lock the transaction holds, all at once: `lockManager.releaseAllLocks(txnId)`.
3. Remove the transaction from the `transactions` map — it's no longer "active."

Releasing locks only *after* flipping the state matters: any other transaction that was waiting and gets woken up by `notifyWaitingTransactions()` (triggered inside `releaseAllLocks`) can immediately and correctly see this transaction as `COMMITTED` or `ABORTED` if it happens to check.

## Read-only / diagnostic methods

| Method | What it returns |
|---|---|
| `getTransactionState(txnId)` | The transaction's current phase, or `ABORTED` if the ID isn't found (a safe-by-default choice) |
| `getLocksHeldBy(txnId)` | Delegates straight to `lockManager.getResourcesLockedBy(txnId)` |
| `getSystemState()` | A full text dump: every active transaction, its state and metadata, plus the current wait-for graph pulled from `deadlockDetector->GetEdgeList()` — this is what test harnesses print at the very end of a run |
| `getResourceAllocationGraph()` | Delegates to `lockManager.getResourceAllocationGraph()`, i.e. `rag.toString()` |
| `logResourceAllocationGraph(info)` | Delegates to `rag.logToFile(info)` — appends a snapshot to `rag_log.txt` |

## `checkForDeadlocks()` — declared, but not implemented

The header declares this method:

```cpp
bool checkForDeadlocks();
```

and `tests/2pl_test_runner.cpp` calls it (`cm.checkForDeadlocks()`), but **no definition for it exists anywhere in `src/concurrency_manager.cpp`** (or any other `.cpp` file in this repository). This means any build that tries to link `2pl_test_runner.cpp` against the current `concurrency_manager.cpp` will fail at the link step with an "undefined reference" error. This is a genuine, confirmed gap in the current codebase — see [Known Issues](20-known-issues-and-inconsistencies.md) for the full explanation and what actually builds today.

Next: [`Logger`](15-module-logger.md).
