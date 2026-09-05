# 19. Building and Running

## Prerequisites

A C++ compiler with thread-library support. The `makefile` specifically requests `-std=c++23` (C++23), and `tests/test_deadlock_detection.cpp` uses `<barrier>`, a C++20 feature — so you need a reasonably modern compiler (a recent version of GCC or Clang). All build commands link against `pthread` for threading support on Linux/macOS.

## What the `makefile` actually builds

The full `makefile`:

```makefile
run: deadlock_test_runner
	./deadlock_test tests/test4.txt

deadlock_test_runner: tests/test_deadlock_detection.cpp src/concurrency_manager.cpp src/lock_manager.cpp src/deadlock_detector.cpp src/transaction.cpp src/logger.cpp src/resource_manager.cpp
	g++ -std=c++23 -o deadlock_test tests/test_deadlock_detection.cpp src/concurrency_manager.cpp src/lock_manager.cpp src/deadlock_detector.cpp src/transaction.cpp src/logger.cpp src/resource_manager.cpp -pthread

clean:
	rm -f 2pl_test_runner* deadlock_test*
	rm -f *.log
```

There are exactly two real targets:

- **`make run`** (or just `make`, since `run` is the first target and therefore the default) — builds `deadlock_test_runner` (which compiles `tests/test_deadlock_detection.cpp` plus every core `src/*.cpp` file except `src/2pl_test_runner.cpp`, into a binary named `deadlock_test`), then immediately runs it against `tests/test4.txt`.
- **`make clean`** — removes any `2pl_test_runner*` or `deadlock_test*` binaries, plus every `*.log` file.

**There is no makefile target for `2pl_test_runner` or `test_lock_manager`** — reflecting the fact that neither currently builds successfully (see [Known Issues](20-known-issues-and-inconsistencies.md)). The `clean` target's reference to `2pl_test_runner*` is a leftover from when such a target presumably did exist.

## Commands that work today

```bash
# Build and run the full deadlock-detection harness against tests/test4.txt
make run

# Just build, without running
make deadlock_test_runner

# Run against a different test file once built
./deadlock_test tests/dead_test.txt
./deadlock_test tests/test1.txt

# Clean up all generated binaries and logs
make clean
```

## Generating a custom workload and running it

```bash
cd tests
python3 generate_simple.py    # writes test.txt (1000 short transactions, light contention)
python3 generate_diff.py      # writes dead_test.txt (1000 transactions, deliberately deadlock-heavy)
cd ..
make deadlock_test_runner
./deadlock_test tests/dead_test.txt
cat transaction_metrics.txt   # see the Restarts / Deadlock Detected columns
```

(Both scripts write their output filenames as configured at the bottom of the script — check the `output_file=` argument in each `if __name__ == "__main__":` block before running, since they don't take command-line arguments.)

## What does *not* currently work, and why

| Command | Why it fails |
|---|---|
| Compiling `src/2pl_test_runner.cpp` per the top-level `README.md`'s manual `g++` example | Two independent compile/link errors: a call to `ConcurrencyManager::checkForDeadlocks()`, which is declared but never defined; and a call to the non-static member function `ResourceAllocationGraph::initLogFile()` through class-scope syntax with no object instance. See [Known Issues](20-known-issues-and-inconsistencies.md). |
| Compiling `tests/test_lock_manager.cpp` per the top-level `README.md`'s manual `g++` example | Constructs `LockManager lockManager(logger)` with one argument, but the current `LockManager` constructor requires two (`Logger&, ResourceAllocationGraph&`) |
| `./2pl_test_runner tests/test2.txt` / `test3.txt` (from the `README.md`'s "Running Tests" section) | Neither the binary nor these two test files exist in this repository |

If you need `2pl_test_runner.cpp` or `test_lock_manager.cpp` working, they can likely be fixed with modest, targeted changes (implement `checkForDeadlocks()` on `ConcurrencyManager` by delegating to `lockManager.detectDeadlock()` and aborting the returned cycle's lowest-priority transaction similarly to `DeadlockDetector::RunCycleDetection()`; fix the `initLogFile` call to go through an instance, e.g. `rag.initLogFile(...)` with access to a `ResourceAllocationGraph` instance, or make the method `static`; and update `test_lock_manager.cpp` to construct a `ResourceAllocationGraph` and pass it to `LockManager`'s constructor) — but as of this documentation, none of that has been done, and none of these three fixes is required to build or run the fully-working `deadlock_test_runner` target.

Next: [Known Issues and Inconsistencies](20-known-issues-and-inconsistencies.md) — the consolidated, honest list of every discrepancy found while writing this documentation.
