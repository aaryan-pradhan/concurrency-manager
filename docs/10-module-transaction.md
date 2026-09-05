# 10. Module: `Transaction`

**Files:** `include/transaction.h`, `src/transaction.cpp`

## What it represents

One `Transaction` object represents one simulated database transaction: an identity, a current phase/state, the set of resources it currently has locked, and some bookkeeping (when it started, an optional description, and a priority used for deadlock victim selection). It has no knowledge of `LockManager`, `ConcurrencyManager`, or anything else "above" it — it's a small, self-contained record.

## Fields

```cpp
int txnId;
TransactionState state;
std::set<int> locksHeld;
std::chrono::system_clock::time_point startTime;
std::string metadata;
int priority;
```

- `txnId` — the transaction's identifier. Assigned by `ConcurrencyManager::beginTransaction()`.
- `state` — one of `GROWING`, `SHRINKING`, `COMMITTED`, `ABORTED`. See [The Two-Phase Locking Protocol](05-two-phase-locking-protocol.md) for what governs the transitions.
- `locksHeld` — a set of resource IDs. This is the `Transaction`'s *own* record of what it holds; it's separate from (and kept in sync with) the `LockManager`'s lock table, which is the actual source of truth used for granting/denying other transactions' requests.
- `startTime` — captured at construction, used by `getAgeMillis()`.
- `metadata` — a free-form string a caller can attach (e.g., `"Transaction 5"`), purely for logging/debugging; never interpreted by the code.
- `priority` — used by `DeadlockDetector` to decide which transaction to sacrifice when a cycle is found (see [Wait-for Graphs and Victim Selection](08-wait-for-graphs-and-victim-selection.md)). Higher number generally means "more important" in this codebase's convention (the detector aborts the transaction with the **lowest** priority value in a cycle).

## The static transaction registry

Unusually, `Transaction` maintains a **static** (class-wide, not per-instance) lookup table:

```cpp
static std::unordered_map<txn_id_t, Transaction*> active_transactions_;
static void RegisterTransaction(Transaction* txn);
static void UnregisterTransaction(txn_id_t txn_id);
static Transaction* GetTransaction(txn_id_t txn_id);
```

Every `Transaction` registers itself in this global map when constructed and removes itself when destructed. This exists so that code far away from `ConcurrencyManager` — specifically `LockManager` (checking "has this transaction been aborted while I was about to grant/deny its request?") and `DeadlockDetector` (checking a transaction's current priority and state while walking the wait-for graph) — can look up a transaction by ID without needing a reference or pointer threaded all the way through the call stack. It's a pragmatic shortcut rather than a "clean" dependency-injected design, but it keeps those lower-level classes decoupled from `ConcurrencyManager`'s transaction map.

**Caution for readers new to C++:** because this map is static, if two `ConcurrencyManager` instances existed in the same process, their transactions would collide in one shared global map (transaction IDs would need to be unique across both). This project's test harnesses only ever construct one `ConcurrencyManager`, so this never comes up in practice, but it's a real limitation of the design.

## Method-by-method

| Method | What it does |
|---|---|
| `Transaction(id, meta, priority)` | Constructs in `GROWING` state, records `startTime`, registers itself statically |
| `~Transaction()` | Unregisters itself statically |
| `acquireLock(resourceId)` | Adds `resourceId` to `locksHeld`, but only if `state == GROWING` — this is the 2PL rule enforced at this layer |
| `releaseLock(resourceId)` | Removes `resourceId` from `locksHeld`. If called while still `GROWING`, first calls `beginShrinking()` to flip the phase (see below) |
| `commit()` | Transitions to `COMMITTED`, unless already `COMMITTED` or `ABORTED` |
| `abort()` | Unconditionally sets `state = ABORTED` |
| `beginShrinking()` | Transitions `GROWING → SHRINKING`; no-op (returns `false`) from any other state |
| `isInGrowingPhase()` | `state == GROWING` |
| `getState()`, `getId()`, `getLocksHeld()`, `getStartTime()`, `getMetadata()`, `getPriority()` | Plain accessors |
| `hasLock(resourceId)` | Is `resourceId` in `locksHeld`? |
| `getAgeMillis()` | Milliseconds since `startTime` |

## A closer look at `releaseLock`'s phase logic

```cpp
bool Transaction::releaseLock(int resourceId) {
    if (locksHeld.find(resourceId) == locksHeld.end()) {
        return false; // can't release what you don't hold
    }
    if (state == TransactionState::GROWING) {
        beginShrinking(); // first-ever release: flip the phase, permanently
    }
    if (state != TransactionState::COMMITTED &&
        state != TransactionState::ABORTED &&
        state != TransactionState::SHRINKING) {
        return false; // shouldn't be reachable given the logic above, but guards against future changes
    }
    locksHeld.erase(resourceId);
    return true;
}
```

Notice that releasing a lock is allowed in `COMMITTED` or `ABORTED` states too — this is what lets `ConcurrencyManager::commitTransaction()` and `abortTransaction()` call `lockManager.releaseAllLocks(txnId)` *after* already having called `txn->commit()` / `txn->abort()`, without that release being rejected.

## Relationship to `LockManager`

It's important to understand that `Transaction::locksHeld` and `LockManager`'s internal `lockTable` are **two separate pieces of bookkeeping that happen to be kept consistent by the calling code**, not one shared structure. Look at `ConcurrencyManager::acquireLock()`:

```cpp
bool acquired = lockManager.acquireLock(txnId, resourceId, lockType, wait); // the real, authoritative grant/deny decision
if (acquired) {
    txn->acquireLock(resourceId); // Transaction's own mirror of "I hold this"
}
```

`LockManager` is the actual authority on who holds what (it's what other transactions' compatibility checks consult). `Transaction::locksHeld` is a convenience mirror, mainly useful for the 2PL phase-transition logic and for reporting (`getLocksHeld()`).

Next: [`LockManager`](11-module-lock-manager.md) — the class that does the real work of granting, queuing, and waking up lock requests.
