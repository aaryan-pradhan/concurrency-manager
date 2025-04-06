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
     * Looks for a cycle using DFS algorithm.
     * If it finds a cycle, stores the transaction id of the youngest 
     * transaction in the cycle in txn_id and returns true.
     * Returns false if no cycle exists.
     */
    bool HasCycle(txn_id_t& txn_id);
    
    /**
     * Returns a list of tuples representing the edges in the graph.
     * A pair (t1,t2) corresponds to an edge from t1 to t2.
     */
    std::vector<std::pair<txn_id_t, txn_id_t>> GetEdgeList();
    
    /**
     * Runs cycle detection in the background.
     * Builds the wait-for graph on the fly and breaks any cycles
     * by aborting the youngest transaction in each cycle.
     */
    void RunCycleDetection();

private:
    // Reference to the lock manager for building the wait-for graph
    LockManager& lock_manager_;
    
    // Reference to the logger for operations
    Logger& logger_;
    
    // Background thread function
    void DetectionThread();
    
    // Helper method for DFS cycle detection
    bool DFS(txn_id_t node, std::unordered_map<txn_id_t, bool>& visited,
             std::unordered_map<txn_id_t, bool>& in_stack, 
             txn_id_t& youngest_txn);
    
    // Builds a wait-for graph on the fly
    void BuildWaitForGraph();
    
    // Map representation of the graph (adjacency list)
    std::unordered_map<txn_id_t, std::vector<txn_id_t>> wait_for_graph_;
    
    // Mutex to protect access to the graph
    std::mutex graph_mutex_;
    
    // Thread control variables
    std::thread detection_thread_;
    std::atomic<bool> running_;
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    
    // Detection interval in milliseconds
    uint64_t detection_interval_ms_;
};