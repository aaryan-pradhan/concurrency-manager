#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include "transaction.h"
#include "lock_manager.h"
#include "logger.h"
#include "deadlock_detector.h"
#include "resource_manager.h" // Add this include

/**
 * @class ConcurrencyManager
 * @brief Coordinates transaction management using Two-Phase Locking protocol
 *        with deadlock detection
 */
class ConcurrencyManager
{
private:
    // Manager for locks on resources
    LockManager lockManager;

    // Logger for operations
    Logger logger;

    // Deadlock detector
    std::unique_ptr<DeadlockDetector> deadlockDetector;

    // Resource allocation graph for deadlock detection
    ResourceAllocationGraph rag;

    // Map of active transactions
    std::unordered_map<int, std::unique_ptr<Transaction>> transactions;

    // Mutex for thread safety
    mutable std::mutex mtx;

    // Next transaction ID
    int nextTxnId;

    /**
     * @brief Gets a transaction by ID, returns nullptr if not found
     * @param txnId ID of the transaction to find
     * @return Pointer to the transaction, or nullptr if not found
     */
    Transaction *getTransaction(int txnId);

public:
    /**
     * @brief Constructs a new ConcurrencyManager
     * @param logFilePath Path to the log file
     * @param detectionIntervalMs Interval for deadlock detection in milliseconds
     */
    explicit ConcurrencyManager(const std::string &logFilePath = "concurrency.log",
                                uint64_t detectionIntervalMs = 200);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~ConcurrencyManager();

    /**
     * @brief Begins a new transaction or restarts an existing one
     * @param metadata Optional metadata for the transaction
     * @param requestedTxnId Transaction ID to use (-1 for new transaction)
     * @param priority Priority value for the transaction (higher = more important)
     * @return Transaction ID that was created or reused
     */
    int beginTransaction(const std::string &metadata = "", int requestedTxnId = -1, int priority = 1);
    
    /**
     * @brief Attempts to acquire a lock for a transaction
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @param lockType Type of lock (SHARED or EXCLUSIVE)
     * @param wait Whether to wait if lock cannot be acquired immediately
     * @return true if lock was acquired, false otherwise
     */
    bool acquireLock(int txnId, int resourceId, LockType lockType, bool wait = true);

    /**
     * @brief Releases a lock held by a transaction
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @return true if lock was released, false otherwise
     */
    bool releaseLock(int txnId, int resourceId);

    /**
     * @brief Commits a transaction and releases all its locks
     * @param txnId ID of the transaction
     * @return true if transaction was committed, false otherwise
     */
    bool commitTransaction(int txnId);

    /**
     * @brief Aborts a transaction and releases all its locks
     * @param txnId ID of the transaction
     * @param reason Optional reason for abortion
     * @return true if transaction was aborted, false otherwise
     */
    bool abortTransaction(int txnId, const std::string &reason = "");

    /**
     * @brief Gets the state of a transaction
     * @param txnId ID of the transaction
     * @return TransactionState enum value, or ABORTED if transaction not found
     */
    TransactionState getTransactionState(int txnId);

    /**
     * @brief Gets information about locks held by a transaction
     * @param txnId ID of the transaction
     * @return Set of resource IDs locked by the transaction
     */
    std::set<int> getLocksHeldBy(int txnId);

    /**
     * @brief Gets a summary of the system state for debugging
     * @return String representation of the concurrency manager state
     */
    std::string getSystemState() const;

    /**
     * @brief Check for a deadlock in the system
     * @return true if a deadlock was found and resolved, false otherwise
     */
    bool checkForDeadlocks();

    /**
     * @brief Get a string representation of the resource allocation graph
     * @return String representation of the graph
     */
    std::string getResourceAllocationGraph() const;

    /**
     * @brief Log the current state of the resource allocation graph to a file
     * @param transactionInfo Additional information about the current transaction
     */
    void logResourceAllocationGraph(const std::string &transactionInfo = "") const;
};