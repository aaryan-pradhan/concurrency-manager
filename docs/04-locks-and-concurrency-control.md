# 4. Locks and Concurrency Control

## The core idea

A **lock** is a permission slip a transaction must obtain before touching a piece of data (a "resource"). If transaction A holds a lock on resource `R`, the lock manager can refuse or delay any other transaction's conflicting request for `R` until A gives it up. This is how the database prevents the lost-update scenario from [Databases and Transactions](02-databases-and-transactions.md): the withdrawal transaction simply cannot read the balance while the deposit transaction holds a write lock on it.

## Two kinds of locks

This project implements the two most fundamental lock types, defined in `include/lock_manager.h`:

```cpp
enum class LockType {
    SHARED,    // for reading
    EXCLUSIVE  // for writing
};
```

- **SHARED lock** ("read lock"): many transactions can hold a shared lock on the same resource simultaneously. This is safe because reading doesn't change anything.
- **EXCLUSIVE lock** ("write lock"): only one transaction may hold this, and while it's held, no other transaction may hold *any* lock (shared or exclusive) on that resource.

## The compatibility matrix

Whether a new lock request can be granted depends only on what's already granted on that resource:

| Already held ↓ / Requested → | SHARED | EXCLUSIVE |
|---|:---:|:---:|
| *(nothing held)* | ✅ Grant | ✅ Grant |
| SHARED (by others) | ✅ Grant | ❌ Wait |
| EXCLUSIVE (by another txn) | ❌ Wait | ❌ Wait |
| EXCLUSIVE (by the *same* txn) | ✅ Grant (already stronger) | ✅ Grant |

This exact table is implemented in `LockManager::isCompatible()` — see [the LockManager module](11-module-lock-manager.md) for the line-by-line explanation.

## What happens when a lock can't be granted?

The requesting transaction has, conceptually, three options:

1. **Wait** — sit in a queue until the lock becomes free. This is what this project does by default (`wait = true`).
2. **Give up immediately** — fail fast rather than wait. The code supports this too (`wait = false`), used in some of the unit tests to check that a conflicting request is correctly rejected.
3. **Time out** — wait for a while, then give up if the lock still isn't available. The `LockManager` header declares a `lockTimeout` of 5 seconds for this purpose, but as documented in [Known Issues](20-known-issues-and-inconsistencies.md), the current waiting code never actually applies it — waits are unbounded until either the lock frees up or the transaction gets aborted by the deadlock detector.

## Lock upgrades

If a transaction already holds a SHARED lock on a resource and later needs to write to it, it doesn't need to release the shared lock and ask for a fresh exclusive one — it can request an **upgrade**. `LockManager::upgradeLock()` handles this: if no other transaction is holding a conflicting lock, it converts the existing SHARED lock in place to EXCLUSIVE. If another transaction also holds a SHARED lock on the same resource, the upgrade must wait (see `tests/test1.txt` for a worked example, where T1 and T2 both hold shared locks on `E`, `F`, `G` and then both try to upgrade — a classic **upgrade deadlock** setup).

## Where locks live: the lock table

The `LockManager` keeps one central data structure:

```cpp
std::unordered_map<int, std::vector<LockRequest>> lockTable;
```

This maps a resource ID to a list of `LockRequest` records (each holding a transaction ID, a lock type, and a `granted` flag — `true` if the lock is currently held, `false` if it's still queued and waiting). Every lock operation — acquire, release, upgrade — is really just inserting into, scanning, or removing from this map, all done under the protection of a mutex (see [Threads and Race Conditions](03-concurrency-threads-and-race-conditions.md)).

## Why locking alone isn't enough

Locking prevents transactions from *corrupting* each other's data, but it introduces a new problem of its own: if transaction A is waiting for a lock held by B, and B is waiting for a lock held by A, neither will ever proceed. This is a **deadlock**, and it's the subject of the next several documents, starting with [The Two-Phase Locking Protocol](05-two-phase-locking-protocol.md), which describes the *rule* for when locks may be acquired and released, and then [Deadlocks, Explained](06-deadlocks-explained.md).
