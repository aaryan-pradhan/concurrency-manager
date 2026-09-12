#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>
#include <set>
#include "logger.h"

// Forward declarations
class Transaction;
class LockManager;
using txn_id_t = uint64_t;

/**
 * DeadlockDetector - Implements a wait-for graph and cycle detection algorithm
 * to detect and resolve deadlocks between transactions.
 */
class DeadlockDetector {
public:
    /**
     * Constructor - starts the background detection thread
     * @param lock_manager Reference to the lock manager for building wait-for graph
     * @param logger Reference to the logger for logging operations
     * @param detection_interval_ms Interval between deadlock detection runs
     */
    DeadlockDetector(LockManager& lock_manager, Logger& logger, uint64_t detection_interval_ms = 200);

    /**
     * Destructor - stops the background detection thread
     */
    ~DeadlockDetector();

    /**
     * Adds an edge in the wait-for graph from t1 to t2.
     * If the edge already exists, does nothing.
     */
    void AddEdge(txn_id_t t1, txn_id_t t2);

    /**
     * Removes an edge from t1 to t2 from the wait-for graph.
     * If no such edge exists, does nothing.
     */
    void RemoveEdge(txn_id_t t1, txn_id_t t2);

    /**
     * Looks for a cycle using DFS. If it finds one, stores in txn_id the member of that
     * cycle with the lowest priority (ties: the highest transaction id) and returns true.
     * Only nodes on the cycle itself are candidates, never nodes merely waiting on it.
     * Returns false if no cycle exists.
     */
    bool HasCycle(txn_id_t& txn_id);

    /**
     * Returns a list of tuples representing the edges in the graph.
     * A pair (t1,t2) corresponds to an edge from t1 to t2.
     */
    std::vector<std::pair<txn_id_t, txn_id_t>> GetEdgeList();

    /**
     * Runs one detection pass: builds the wait-for graph from a consistent lock-table
     * snapshot and breaks every cycle by aborting one victim per cycle.
     * @return number of cycles broken
     */
    int RunCycleDetection();

private:
    // Reference to the lock manager for building the wait-for graph
    LockManager& lock_manager_;

    // Reference to the logger for operations
    Logger& logger_;

    // Background thread function
    void DetectionThread();

    // DFS that records the current path so the exact cycle can be extracted (graph_mutex_ held)
    bool FindCycle(txn_id_t node, std::unordered_map<txn_id_t, bool>& visited,
                   std::unordered_map<txn_id_t, bool>& in_stack,
                   std::vector<txn_id_t>& path, std::vector<txn_id_t>& cycle);

    // Unlocked versions (graph_mutex_ held)
    void AddEdgeInternal(txn_id_t t1, txn_id_t t2);
    bool HasCycleInternal(txn_id_t& txn_id);

    // Builds a wait-for graph on the fly (graph_mutex_ held)
    void BuildWaitForGraph();

    // Map representation of the graph (adjacency list)
    std::unordered_map<txn_id_t, std::vector<txn_id_t>> wait_for_graph_;

    // Mutex to protect access to the graph
    std::mutex graph_mutex_;

    // Serializes whole detection passes (background thread and on-demand calls)
    std::mutex run_mutex_;

    // Detection interval in milliseconds
    uint64_t detection_interval_ms_;

    // Thread control variables
    std::atomic<bool> running_;
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    std::thread detection_thread_;
};
