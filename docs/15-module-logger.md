# 15. Module: `Logger`

**Files:** `include/logger.h`, `src/logger.cpp`

## Purpose

Every other class in this project (`LockManager`, `ResourceAllocationGraph`, `DeadlockDetector`, `ConcurrencyManager`) needs to record what it's doing — which lock was granted to which transaction, when a deadlock cycle was found, which transaction got aborted, and so on — both for a human debugging a test run and, in principle, as a record of the system's behavior over time. `Logger` is the shared utility that all of them write through, so every message ends up in one consistent, timestamped, thread-safe stream.

## Why it needs its own mutex

Every simulated transaction runs on its own thread (see [Threads and Race Conditions](03-concurrency-threads-and-race-conditions.md)), and many of them may try to log a message at the same instant. Without synchronization, two threads writing to the same `std::ofstream` at the same time could interleave their characters into a garbled, unreadable line. `Logger::log()` takes `mtx` for the whole duration of formatting and writing one message, so log lines are always written atomically, one complete line at a time, however many threads are calling in concurrently.

```cpp
void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    // ... build the formatted line, write to file, optionally also to console
}
```

## Log levels

```cpp
enum class LogLevel { DEBUG, INFO, WARNING, ERROR, FATAL };
```

Ordered from least to most severe. The `Logger` is constructed with a `minLevel`; any message logged below that level is silently dropped. This lets a caller dial verbosity up or down without touching call sites — e.g., `ConcurrencyManager` constructs its `Logger` with console output disabled (`logger(logFilePath, false)`), so day-to-day runs write everything to a log file but keep the terminal quiet, while `tests/test_lock_manager.cpp` constructs its own `Logger` with `LogLevel::DEBUG` and console output on, for a much chattier, interactive test run.

## Two kinds of logging methods

**Structured, domain-specific methods** — one per kind of event this project cares about, so call sites read clearly and consistently instead of hand-writing string concatenation everywhere:

```cpp
void logTransactionStart(int txnId);
void logTransactionCommit(int txnId);
void logTransactionAbort(int txnId, const std::string& reason = "");
void logLockAcquireAttempt(int txnId, int resourceId, const std::string& lockType);
void logLockAcquired(int txnId, int resourceId, const std::string& lockType);
void logLockReleased(int txnId, int resourceId);
void logLockWaiting(int txnId, int resourceId, int holdingTxnId);
void logDeadlockDetectionStart();
void logDeadlockDetected(const std::vector<int>& cycle);
void logDeadlockResolution(int victimTxnId);
void logDeadlockDetectionComplete(bool foundDeadlock);
void logTreeLockRequest(int txnId, int resourceId);
void logTreeLockViolation(int txnId, int resourceId, int violationReason);
```

**Generic methods**, used far more often in practice throughout `LockManager`, `ConcurrencyManager`, etc., because they're more flexible for one-off messages:

```cpp
void debug(const std::string& message);
void info(const std::string& message);
void warning(const std::string& message);
void error(const std::string& message);
void fatal(const std::string& message);
```

If you search the codebase, you'll notice the generic `info()`/`warning()` calls (with hand-built strings like `"T" + std::to_string(txnId) + " acquired ..."`) are used far more heavily than the structured `logLock*`/`logTransaction*` methods — the structured API exists and works, but most of the actual call sites in `LockManager` and `ConcurrencyManager` were written using the generic path instead. Both produce equivalent log output; this is a stylistic inconsistency rather than a functional one.

Note also `logTreeLockRequest` / `logTreeLockViolation`, which reference a "tree protocol" — this refers to the **tree locking protocol**, an alternative to 2PL for hierarchically-organized data (useful background if you ever see `tests/test_tree_protocol.cpp`, which is currently an empty, unimplemented file — see [Known Issues](20-known-issues-and-inconsistencies.md)).

## Output format and destinations

Every log line is timestamped (`getTimestamp()`) and can go to two places at once, controlled independently:

- **A file**, opened in the constructor (`std::ofstream logFile`) and always written to if it opened successfully.
- **The console** (`std::cout`), only if `consoleOutput` is `true` — toggle at any time with `setConsoleOutput(bool)`.

```cpp
Logger(const std::string& filename, bool toConsole = true, LogLevel level = LogLevel::INFO);
```

## Where the log files actually end up

Different programs construct their own `Logger` pointing at different filenames — there is no single, fixed log file for the whole project. See [Metrics and Log Files](18-metrics-and-log-files.md) for the full list of output files each test harness produces and how to read them.

Next: return to [`ConcurrencyManager`](14-module-concurrency-manager.md) if you haven't read it yet, or continue to [The Test-File Mini-Language](16-test-file-language.md) to see how the `.txt` schedules that drive all of this are written.
