// Throughput benchmark with built-in correctness checks.
//
// A fixed set of N generated transactions is executed by a pool of T worker threads
// (T = 1 is the serial baseline, run by the same binary on the same workload).
// Every item holds an integer; a read loads it, a write stores (latest value seen by the
// transaction) + 1, and writes are installed at commit while all locks are still held.
//
// After each run two checks are applied to what actually committed:
//   1. Conflict-serializability: the committed operations, ordered by the sequence number
//      taken while the lock was held, form a precedence graph that must be acyclic.
//   2. No lost updates: each item's final value must equal the number of committed writes to it.
//
// Build with -DLEGACY_API to run the same benchmark against the pre-fix engine API.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <latch>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "../include/concurrency_manager.h"

struct Op {
    bool write;
    int item;
};

struct Event {
    uint64_t seq;
    int txn;   // workload index of the transaction
    char kind; // 'R', 'W' or 'C'
    int item;
};

struct Config {
    int txns = 1000;
    std::vector<int> threads = {1, 2, 4, 8, 16, 32};
    int ops = 4;
    int items = 1000;
    double writeRatio = 0.2;
    int workUs = 0;
    int runs = 5;
    unsigned seed = 42;
    int detectMs = 10;
    int backoffBaseUs = 1000;
    int backoffCapUs = 100000;
    std::string label = "bench";
    std::string csv;
};

struct RunResult {
    double seconds = 0;
    long commits = 0;
    long aborts = 0;
    bool serializable = true;
    int txnsLeftInCycles = 0;
    int lostUpdateItems = 0;
};

static std::vector<int> parseList(const std::string &s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        out.push_back(std::stoi(part));
    }
    return out;
}

static Config parseArgs(int argc, char **argv) {
    Config c;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--txns") c.txns = std::stoi(v);
        else if (k == "--threads") c.threads = parseList(v);
        else if (k == "--ops") c.ops = std::stoi(v);
        else if (k == "--items") c.items = std::stoi(v);
        else if (k == "--write-ratio") c.writeRatio = std::stod(v);
        else if (k == "--work-us") c.workUs = std::stoi(v);
        else if (k == "--runs") c.runs = std::stoi(v);
        else if (k == "--seed") c.seed = static_cast<unsigned>(std::stoul(v));
        else if (k == "--detect-ms") c.detectMs = std::stoi(v);
        else if (k == "--backoff-base-us") c.backoffBaseUs = std::stoi(v);
        else if (k == "--backoff-cap-us") c.backoffCapUs = std::stoi(v);
        else if (k == "--label") c.label = v;
        else if (k == "--csv") c.csv = v;
        else {
            std::cerr << "unknown option " << k << "\n";
            std::exit(2);
        }
    }
    return c;
}

static std::vector<std::vector<Op>> generateWorkload(const Config &c) {
    std::mt19937 rng(c.seed);
    std::uniform_int_distribution<int> item(1, c.items);
    std::bernoulli_distribution isWrite(c.writeRatio);
    std::vector<std::vector<Op>> txns(c.txns);
    for (auto &t : txns) {
        for (int i = 0; i < c.ops; ++i) {
            t.push_back({isWrite(rng), item(rng)});
        }
    }
    return txns;
}

// Precedence graph over committed transactions; returns the number of transactions
// that a topological sort cannot place (members of a cycle or ordered after one).
// 0 means the graph is acyclic, i.e. the committed schedule is conflict-serializable.
static int transactionsInCycles(const std::vector<Event> &history, int numTxns) {
    std::unordered_map<int, std::vector<const Event *>> byItem;
    for (const auto &e : history) {
        if (e.kind != 'C') {
            byItem[e.item].push_back(&e);
        }
    }
    std::vector<std::vector<int>> adj(numTxns);
    std::vector<int> indeg(numTxns, 0);
    std::vector<std::unordered_map<int, bool>> seen(numTxns);
    for (auto &[item, events] : byItem) {
        std::sort(events.begin(), events.end(), [](auto *a, auto *b) { return a->seq < b->seq; });
        for (size_t i = 0; i < events.size(); ++i) {
            for (size_t j = i + 1; j < events.size(); ++j) {
                const Event *a = events[i];
                const Event *b = events[j];
                if (a->txn != b->txn && (a->kind == 'W' || b->kind == 'W') && !seen[a->txn][b->txn]) {
                    seen[a->txn][b->txn] = true;
                    adj[a->txn].push_back(b->txn);
                    indeg[b->txn]++;
                }
            }
        }
    }
    std::vector<int> queue;
    for (int t = 0; t < numTxns; ++t) {
        if (indeg[t] == 0) queue.push_back(t);
    }
    int removed = 0;
    while (!queue.empty()) {
        int t = queue.back();
        queue.pop_back();
        removed++;
        for (int u : adj[t]) {
            if (--indeg[u] == 0) queue.push_back(u);
        }
    }
    return numTxns - removed;
}

static RunResult runOnce(const Config &c, const std::vector<std::vector<Op>> &workload, int numThreads, int runIndex) {
    std::string logFile = c.label + "_engine.log";
#ifdef LEGACY_API
    ConcurrencyManager cm(logFile, c.detectMs);
#else
    ConcurrencyManager cm(logFile, c.detectMs, LogLevel::WARNING);
#endif

    std::vector<std::atomic<long long>> values(c.items + 1);
    for (auto &v : values) v.store(0);

    std::atomic<uint64_t> seq{0};
    std::atomic<int> nextTxn{0};
    std::atomic<long> commits{0};
    std::atomic<long> aborts{0};
    std::vector<std::vector<Event>> histories(numThreads);
    std::latch start(1);

    auto worker = [&](int w) {
        std::mt19937 rng(c.seed * 7919u + static_cast<unsigned>(w * 104729 + runIndex));
        std::uniform_int_distribution<int> jitter(70, 129);
        start.wait();
        int idx;
        while ((idx = nextTxn.fetch_add(1)) < c.txns) {
            const auto &ops = workload[idx];
            int txnId = -1;
            int priority = 1;
            int attempt = 0;
            while (true) {
                txnId = cm.beginTransaction("", txnId, priority);
                std::vector<Event> local;
                std::unordered_map<int, long long> seenValue; // latest value this transaction has read or written
                std::map<int, long long> writes;
                bool failed = false;

                for (const auto &op : ops) {
                    bool ok = cm.acquireLock(txnId, op.item, op.write ? LockType::EXCLUSIVE : LockType::SHARED, true);
#ifdef LEGACY_API
                    // The old engine reports some successful waits as failures; the original harness
                    // carried on unless the transaction had been aborted, so do the same here.
                    ok = ok || cm.getTransactionState(txnId) != TransactionState::ABORTED;
#endif
                    if (!ok) {
                        failed = true;
                        break;
                    }
                    local.push_back({seq.fetch_add(1), idx, op.write ? 'W' : 'R', op.item});
                    if (c.workUs > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(c.workUs));
                    }
                    auto it = seenValue.find(op.item);
                    long long current = it != seenValue.end() ? it->second : values[op.item].load();
                    if (op.write) {
                        seenValue[op.item] = current + 1;
                        writes[op.item] = current + 1;
                    } else {
                        seenValue[op.item] = current;
                    }
                }

                if (!failed) {
                    auto install = [&]() {
                        for (const auto &[item, value] : writes) values[item].store(value);
                        local.push_back({seq.fetch_add(1), idx, 'C', 0});
                    };
#ifdef LEGACY_API
                    install();
                    bool committed = cm.commitTransaction(txnId);
#else
                    bool committed = cm.commitTransaction(txnId, install);
#endif
                    if (committed) {
                        auto &h = histories[w];
                        h.insert(h.end(), local.begin(), local.end());
                        commits++;
                        break;
                    }
                }

                // Aborted (normally by the deadlock detector): release everything and retry
                if (cm.getTransactionState(txnId) != TransactionState::ABORTED) {
                    cm.abortTransaction(txnId, "benchmark retry");
                }
                aborts++;
                attempt++;
                priority++;
                long base = std::min<long>(static_cast<long>(c.backoffBaseUs) << std::min(attempt, 20), c.backoffCapUs);
                std::this_thread::sleep_for(std::chrono::microseconds(base * jitter(rng) / 100));
            }
        }
    };

    std::vector<std::thread> pool;
    for (int w = 0; w < numThreads; ++w) {
        pool.emplace_back(worker, w);
    }
    auto t0 = std::chrono::steady_clock::now();
    start.count_down();
    for (auto &t : pool) t.join();
    auto t1 = std::chrono::steady_clock::now();

    RunResult r;
    r.seconds = std::chrono::duration<double>(t1 - t0).count();
    r.commits = commits.load();
    r.aborts = aborts.load();

    std::vector<Event> history;
    for (auto &h : histories) history.insert(history.end(), h.begin(), h.end());
    r.txnsLeftInCycles = transactionsInCycles(history, c.txns);
    r.serializable = r.txnsLeftInCycles == 0;

    std::vector<long long> expected(c.items + 1, 0);
    for (const auto &e : history) {
        if (e.kind == 'W') expected[e.item]++;
    }
    // A transaction that writes an item twice increments it twice
    for (int item = 1; item <= c.items; ++item) {
        if (values[item].load() != expected[item]) r.lostUpdateItems++;
    }
    return r;
}

int main(int argc, char **argv) {
    Config c = parseArgs(argc, argv);
    auto workload = generateWorkload(c);

    std::ofstream csv;
    if (!c.csv.empty()) {
        bool exists = std::ifstream(c.csv).good();
        csv.open(c.csv, std::ios::app);
        if (!exists) {
            csv << "label,threads,txns,ops,items,write_ratio,work_us,detect_ms,run,seconds,throughput,commits,aborts,serializable,unsortable_txns,lost_update_items\n";
        }
    }

    std::cout << "workload " << c.label << ": " << c.txns << " txns x " << c.ops << " ops, " << c.items
              << " items, write ratio " << c.writeRatio << ", work " << c.workUs << " us/op, seed " << c.seed << "\n";
    std::cout << std::left << std::setw(8) << "threads" << std::setw(26) << "throughput txn/s (median)"
              << std::setw(22) << "min .. max" << std::setw(12) << "aborts/run" << "checks\n";

    double serialMedian = 0;
    for (int t : c.threads) {
        std::vector<double> tp;
        long abortSum = 0;
        int failedChecks = 0;
        for (int run = 0; run < c.runs; ++run) {
            RunResult r = runOnce(c, workload, t, run);
            double throughput = r.commits / r.seconds;
            tp.push_back(throughput);
            abortSum += r.aborts;
            if (!r.serializable || r.lostUpdateItems > 0 || r.commits != c.txns) failedChecks++;
            if (csv) {
                csv << std::defaultfloat << c.label << ',' << t << ',' << c.txns << ',' << c.ops << ',' << c.items << ',' << c.writeRatio << ','
                    << c.workUs << ',' << c.detectMs << ',' << run << ',' << std::fixed << std::setprecision(4) << r.seconds << ','
                    << std::setprecision(2) << throughput << ',' << r.commits << ',' << r.aborts << ','
                    << (r.serializable ? "yes" : "no") << ',' << r.txnsLeftInCycles << ',' << r.lostUpdateItems << "\n";
            }
        }
        std::sort(tp.begin(), tp.end());
        double median = tp[tp.size() / 2];
        if (t == c.threads.front()) serialMedian = median;
        std::ostringstream range;
        range << std::fixed << std::setprecision(1) << tp.front() << " .. " << tp.back();
        std::cout << std::left << std::setw(8) << t << std::setw(26) << std::fixed << std::setprecision(1) << median
                  << std::setw(22) << range.str() << std::setw(12) << (abortSum / c.runs)
                  << (failedChecks == 0 ? "all runs serializable, no lost updates" : std::to_string(failedChecks) + " run(s) FAILED")
                  << "   (" << std::setprecision(2) << median / serialMedian << "x vs " << c.threads.front() << " thread)\n";
    }
    return 0;
}
