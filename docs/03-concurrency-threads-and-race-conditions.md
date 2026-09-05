# 3. Threads and Race Conditions

This document explains the operating-systems-level building blocks the code uses. If you're comfortable with threads, mutexes, and condition variables already, skim the summary table at the bottom and move on.

## What is a thread?

A running program is a **process**. A process can split its work across multiple **threads** — independent sequences of instructions that all share the same memory and run "at the same time" (either truly in parallel on different CPU cores, or interleaved on one core so fast it looks parallel).

In this project, each simulated transaction runs on its own thread. Look at `tests/test_deadlock_detection.cpp`: for every transaction parsed out of a test file, the program does:

```cpp
threads.emplace_back(runTransaction, txnNum, ops, 1);
```

This literally spawns an operating-system thread that runs the `runTransaction` function — meaning dozens or hundreds of transactions can be genuinely executing their lock-acquire/release logic concurrently, exercising the exact race conditions the `LockManager` has to guard against.

## What is a race condition?

A **race condition** happens when two threads access the same piece of shared memory at the same time, and at least one of them is writing, and the final outcome depends on the unpredictable timing of which thread "wins the race" to run first. The bank example in [Databases and Transactions](02-databases-and-transactions.md) is a race condition at the transaction level; the same problem exists at the level of raw C++ variables.

Example: two threads both run `counter++` on a shared `int counter`. That single line is actually three CPU steps: read `counter`, add 1, write it back. If both threads read the same starting value before either writes back, one increment is lost. This is exactly the same *shape* of bug as the lost-update problem — it's why the `LockManager`'s internal data structures (like its `lockTable`) need their own protection, entirely separate from the transaction-level locks it hands out.

## Mutexes: the tool that prevents race conditions

A **mutex** (mutual exclusion lock) is a low-level lock provided by the operating system/C++ standard library. Only one thread can "hold" a mutex at a time; any other thread that tries to acquire it simply blocks (waits) until the holder releases it.

```cpp
std::mutex mtx;

void safeIncrement() {
    std::lock_guard<std::mutex> lock(mtx); // acquire mtx; released automatically when `lock` goes out of scope
    counter++;
}
```

Every class in this project that can be touched by multiple threads has its own internal mutex:

- `LockManager` has `std::mutex mtx` protecting its lock table.
- `ResourceAllocationGraph` has `std::recursive_mutex mtx` protecting its edge maps.
- `ConcurrencyManager` has `std::mutex mtx` protecting its map of active transactions.
- `Logger` has `std::mutex mtx` so log lines from different threads don't get garbled together.

**Important distinction:** these mutexes are an *implementation detail* used to keep the C++ objects themselves from corrupting their own internal data. They are completely different from the **transaction locks** (shared/exclusive locks on resource IDs) that are this project's actual subject matter. A mutex protects a data structure in memory for a few instructions; a transaction lock protects a logical database resource for the lifetime of a transaction, potentially seconds.

## Condition variables: waiting efficiently

If a transaction wants a lock that's currently unavailable, it needs to **wait** until it becomes available. The naive way to wait — repeatedly checking "is it free yet? is it free yet?" in a loop (called **busy-waiting** or **spinning**) — wastes CPU. The proper tool is a **condition variable**, which lets a thread sleep efficiently until another thread explicitly wakes it up.

```cpp
std::condition_variable cv;

// Waiting thread:
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, /* predicate */ [] { return conditionIsTrue; });

// Another thread, after changing state:
cv.notify_all(); // wake up everyone waiting on cv
```

`LockManager::waitForLock()` uses exactly this pattern: a transaction that can't get a lock immediately calls `cv.wait(lock, canAcquireLock)`, where `canAcquireLock` is a small function checking "has my request become grantable, or has my transaction been aborted?" Whenever any transaction releases a lock, `LockManager::notifyWaitingTransactions()` calls `cv.notify_all()`, waking every waiting thread up to re-check that predicate. See [the LockManager module](11-module-lock-manager.md) for the full walkthrough.

## Atomics

An `std::atomic<int>` (used in the test harnesses for counters like `activeThreads`) is a special integer type where operations like `++` and `--` are guaranteed to be a single, uninterruptible step — no mutex needed for simple counters like this.

## Summary table

| Term | One-line meaning | Used where in this project |
|---|---|---|
| Process | A running program | The compiled test executables |
| Thread | An independent stream of execution sharing memory with its process | One per simulated transaction |
| Race condition | Bug from unsynchronized concurrent access to shared memory | What mutexes and the locking protocol both exist to prevent |
| Mutex | Lock ensuring only one thread touches a data structure at a time | Every stateful class (`LockManager`, `ResourceAllocationGraph`, etc.) |
| Condition variable | Lets a thread sleep until woken by another thread | `LockManager`'s wait queue |
| Atomic | Integer/flag type safe to update without a mutex | Thread counters, flags in test harnesses |

Next: [Locks and Concurrency Control](04-locks-and-concurrency-control.md) — how these low-level tools are used to build the transaction-level locking this whole project is about.
