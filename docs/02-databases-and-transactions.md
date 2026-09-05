# 2. Databases and Transactions

## What is a database, really?

At its core, a database is a program whose job is to store data reliably and let multiple other programs (or people) read and change that data over time. Think of it as a very disciplined shared notebook: many people want to read from it and write in it, and the notebook's job is to make sure nobody's edits get lost, contradicted, or seen half-finished by someone else.

## What is a transaction?

A **transaction** is a group of one or more read/write operations that the database treats as a single, indivisible unit of work. The classic example is a bank transfer:

```
BEGIN TRANSACTION
  READ  balance of Account A
  WRITE balance of Account A (decrease by $100)
  READ  balance of Account B
  WRITE balance of Account B (increase by $100)
COMMIT
```

The whole point of calling this a "transaction" rather than "four separate operations" is that it must either happen *entirely* or *not at all*. If the program crashes after decreasing Account A's balance but before increasing Account B's, $100 has simply disappeared — unacceptable for a bank. Transactions exist to prevent that.

A transaction ends in one of two ways:

- **Commit** — all of its changes become permanent and visible to everyone else.
- **Abort** (a.k.a. rollback) — all of its changes are discarded, as if the transaction never ran.

In this project, these two outcomes are represented directly: see `Transaction::commit()` and `Transaction::abort()` in [the Transaction module](10-module-transaction.md).

## ACID, briefly

You'll often see transactions described by the acronym **ACID**:

- **Atomicity** — all-or-nothing (explained above).
- **Consistency** — a transaction moves the database from one valid state to another valid state (e.g., total money in the bank doesn't change after a transfer).
- **Isolation** — transactions running at the same time don't interfere with each other, even though they're physically overlapping. **This project is entirely about Isolation.**
- **Durability** — once committed, changes survive crashes (usually via writing to disk). This project does not deal with durability at all — everything is in memory.

## Why isolation is hard

If you ran transactions strictly one at a time — start one, let it fully finish, then start the next — isolation would be trivial, but the database would be painfully slow: every user would have to wait in a single-file line. Real systems want to run many transactions **concurrently** (overlapping in time, often on multiple CPU cores) for performance, while still producing a result that looks *as if* they ran one at a time. That illusion is called **serializability**, and the mechanisms that create it are called **concurrency control**.

This project's `ConcurrencyManager` is a concurrency-control mechanism. It doesn't stop transactions from running at the same time — it stops them from touching the *same data* at the same time in ways that would break the illusion.

## Reads and writes, and why the distinction matters

Two transactions that only **read** the same data can safely run at the same time — reading doesn't change anything, so there's nothing to conflict over. But if even one of them **writes**, you have a potential conflict, as in the bank example above. This is why this project's locks come in two flavors — **shared** (for reading) and **exclusive** (for writing) — covered in [Locks and Concurrency Control](04-locks-and-concurrency-control.md).

## Mapping this onto the code

In this project:

- A transaction is an instance of the `Transaction` class, identified by an integer transaction ID (`txnId`), created via `ConcurrencyManager::beginTransaction()`.
- A "read" is simulated by acquiring a **shared** lock on a resource ID.
- A "write" is simulated by acquiring an **exclusive** lock on a resource ID.
- There's no actual data being read or written — the simulation only cares about the *locking* behavior, not the *values*.

Continue to [Threads and Race Conditions](03-concurrency-threads-and-race-conditions.md) to understand the lower-level mechanism (threads, mutexes) that the C++ code uses to implement all of this safely.
