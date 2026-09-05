# 1. What Is This Project?

## The one-sentence version

This project is a **simulator of the traffic-control system inside a database**: the part that decides which transaction gets to touch which piece of data, and in what order, when many transactions are running at the same time.

## The problem it solves

Imagine a bank's database. Two people are both trying to update the same account balance at the exact same instant — say, one is depositing money and the other is withdrawing money. If the database naively let both operations run at once without any coordination, you could get this sequence:

1. Deposit process reads balance: $100
2. Withdrawal process reads balance: $100
3. Deposit process adds $50, writes balance: $150
4. Withdrawal process subtracts $30 (based on the $100 it read earlier, not the $150), writes balance: $70

The correct final balance should be $100 + $50 − $30 = $120, but the database now shows $70. Money has effectively vanished. This is called a **lost update**, and it happens because the two operations were allowed to interleave without any rule governing who goes first.

Real databases prevent this using **concurrency control**: a set of rules and mechanisms that make concurrent transactions behave *as if* they had run one after another (even though, for performance, they're actually overlapping in time). This project implements one specific, classic concurrency-control mechanism: **Two-Phase Locking (2PL)**, plus the machinery needed to detect and recover from a side effect it can cause: **deadlocks**.

## What "simulator" means here

This is not a full database. There's no actual storage engine, no SQL parser, no disk files holding rows of data. Instead:

- A "piece of data" is just an integer ID called a **resource ID** (e.g., resource `1001`).
- A "transaction" is a sequence of operations like "read resource 1001", "write resource 1002", "commit".
- The project provides a library (`ConcurrencyManager` and friends) that transactions call into to ask for permission before reading or writing a resource.
- Test programs simulate many transactions running in parallel threads, each following a script, and record what happens.

This lets you study and observe locking and deadlock behavior in isolation, without needing an entire DBMS around it.

## What you'll find in the repository

| Area | What it contains |
|---|---|
| `include/`, `src/` | The actual concurrency-control library: `Transaction`, `LockManager`, `ResourceAllocationGraph`, `DeadlockDetector`, `ConcurrencyManager`, `Logger` |
| `tests/` | Programs that drive the library with multi-threaded workloads, plus `.txt` files describing those workloads, plus Python scripts that generate more `.txt` files |
| `makefile` | Build instructions |

## Who wrote this and why

Judging from the commit history, this looks like a class project for a Database Management Systems (DBMS) and/or Operating Systems (OS) course — concurrency control and deadlock detection are core topics in both fields. The two disciplines overlap here on purpose: **locks, threads, and deadlocks are OS concepts**; **transactions, 2PL, and serializability are DBMS concepts**. This project sits exactly at that intersection.

## How to use this documentation

If you have zero background in databases or operating systems, read Part 1 of the [index](README.md) straight through before looking at any code — it defines every term the code uses. If you already know the theory and just want to understand the code, skip to [Architecture Overview](09-architecture-overview.md).
