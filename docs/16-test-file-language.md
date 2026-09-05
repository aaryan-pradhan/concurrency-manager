# 16. The Test-File Mini-Language

The `.txt` files in `tests/` (like `tests/test1.txt`, `tests/dead_test.txt`) aren't data — they're small **programs**, written in a tiny purpose-built language that describes a set of transactions and the operations each one performs, in the order they appear in the file. The test harnesses (`src/2pl_test_runner.cpp` and `tests/test_deadlock_detection.cpp`) parse this language with regular expressions and turn it into actual multi-threaded execution. See [The Test Harnesses](17-test-harnesses.md) for how the parsed result is executed.

## The six commands

| Syntax | Meaning | Example |
|---|---|---|
| `START T<n>` | Begin transaction number `n` | `START T1` |
| `R<n>(<item>)` | Transaction `n` reads data item `<item>` (acquires a SHARED lock) | `R1(E)` |
| `W<n>(<item>)` | Transaction `n` writes data item `<item>` (acquires an EXCLUSIVE lock) | `W1(F)` |
| `C<n>` | Transaction `n` commits | `C1` |
| `A<n>` | Transaction `n` aborts | `A1` |
| `RELEASE T<n>(<item>)` | Transaction `n` releases its lock on `<item>` early, without committing | `RELEASE T1(E)` |

`<item>` is any alphanumeric name (`A`, `E`, `F`, `item42`, ...) — it does **not** have to be a resource ID number. The mapping from item names to actual integer resource IDs is done by the test harness itself, not the language: a global `std::map<std::string, int>` (`dataItemToResourceId` / `getResourceId()`) assigns each distinct item name the next available integer the first time it's seen, so `R1(E)` and `R2(E)` both end up locking the *same* underlying resource ID.

## Comments and blank lines

Lines starting with `//` are comments and are ignored, as are blank lines:

```
// Basic 2PL test with potential lock conflicts
START T1
START T2

R1(E)   // T1 reads item E (gets shared lock on E)
```

`tests/test_deadlock_detection.cpp`'s parser also strips **inline** comments (anything after `//` on a line that has other content before it), while `src/2pl_test_runner.cpp`'s parser only recognizes a comment if the *entire line* starts with `//`. This is a small but real difference between the two parsers — see [The Test Harnesses](17-test-harnesses.md).

## How operations are grouped

The parser doesn't execute commands in the literal top-to-bottom order they appear in the file. Instead, it groups every line by its transaction number (`T<n>`) into one ordered list per transaction, and then hands each transaction's list off to its own dedicated thread, which runs *that* list from the top. This means the actual execution order across *different* transactions is only loosely suggested by the file's line order (and further scrambled by real thread scheduling and small randomized sleeps) — the file's line order mainly determines the order of operations **within a single transaction**, which is what actually matters for correctness testing.

## A worked example: `tests/test1.txt`

```
START T1
START T2

R1(E)   // T1 reads item E (gets shared lock on E)
R2(G)   // T2 reads item G (gets shared lock on G)
R1(F)   // T1 reads item F (gets shared lock on F)
R2(F)   // T2 reads item F (gets shared lock on F, shared with T1)
R1(G)   // T1 reads item G (gets shared lock on G, shared with T2)
R2(E)   // T2 reads item E (gets shared lock on E, shared with T1)

W1(F)   // T1 tries to upgrade to exclusive lock on F (T2 holds shared lock)
W2(E)   // T2 tries to upgrade to exclusive lock on E (T1 holds shared lock)
W1(G)   // T1 tries to upgrade to exclusive lock on G (T2 holds shared lock)

C2      // T2 commits - all its locks are released
C1      // T1 commits - all its locks are released
```

Walking through it: T1 and T2 first both acquire shared (read) locks on overlapping items (`E`, `F`, `G`), which is fine — shared locks don't conflict with each other. Then both try to **upgrade** to exclusive (write) locks on items the *other* transaction also holds shared — this is the classic upgrade-deadlock setup from [Locks and Concurrency Control](04-locks-and-concurrency-control.md) and [The LockManager module](11-module-lock-manager.md). Depending on timing and whether a deadlock detector is running, this either resolves once one side commits and releases (freeing the other's upgrade), or triggers deadlock detection if both are stuck waiting on each other simultaneously.

## Programmatically generating test files

Hand-writing large schedules is impractical, so two Python scripts in `tests/` generate them:

- **`tests/generate_simple.py`** produces `num_transactions` short, independent transactions (default 1000), each with a small random number of reads/writes over a shared pool of letters `A`–`Z`, biased toward reads (`read_ratio`). This creates *contention* (many transactions touching the same small set of items) but with a low read/write ratio deliberately kept low enough that deadlocks aren't the main point — it's more of a general load/throughput test.
- **`tests/generate_diff.py`** produces `deadlock_1000_transactions.txt` (its function is literally named `generate_large_deadlock_heavy_workload`) — it deliberately creates small batches of transactions that each touch the *same 3 items*, but in **alternating order** (`access_order[::-1]` for every other transaction in a batch), which is a textbook way to manufacture circular wait conditions on purpose: if transaction A locks items in order `[X, Y, Z]` while transaction B locks them in order `[Z, Y, X]`, an unlucky interleaving reliably produces a wait-for cycle. This file exists specifically to exercise the deadlock detector under heavy, repeated deadlock pressure.

Neither script is invoked automatically by the build or by any test harness — they're standalone tools you run manually (`python3 tests/generate_simple.py`) to produce a `.txt` file, which you then pass as a command-line argument to one of the compiled test programs.

Next: [The Test Harnesses](17-test-harnesses.md) — the programs that actually parse and execute these files.
