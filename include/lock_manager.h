#pragma once

#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "logger.h"
#include "transaction.h"
#include "resource_manager.h"  // Add this include

/**
 * @enum LockType
 * @brief Types of locks that can be acquired on resources
 */
enum class LockType
{
    SHARED,   // Multiple transactions can hold shared locks on a resource
    EXCLUSIVE // Only one transaction can hold an exclusive lock on a resource
};

/**
 * @brief Convert LockType to a readable string representation
 * @param type The lock type
 * @return String representation of the lock type
 */
inline std::string lockTypeToString(LockType type)
{
    return type == LockType::SHARED ? "SHARED" : "EXCLUSIVE";
}

/**
 * @struct LockRequest
 * @brief Represents a request for a lock by a transaction
 */
struct LockRequest
{
    int transactionId;                                 // ID of the requesting transaction
    LockType type;                                     // Type of lock requested
    bool granted;                                      // Whether the lock has been granted
    std::chrono::system_clock::time_point requestTime; // When the request was made

    LockRequest(int txnId, LockType lockType)
        : transactionId(txnId),
          type(lockType),
          granted(false),
          requestTime(std::chrono::system_clock::now()) {}
};

/**
 * @class LockManager
 * @brief Manages locks on resources implementing Two-Phase Locking protocol
 */
class LockManager
{
private:
    // Lock table: maps resource IDs to the list of lock requests for that resource
    std::unordered_map<int, std::vector<LockRequest>> lockTable;

    // Mutex for thread safety
    mutable std::mutex mtx;

    // Logger for recording lock operations
    Logger &logger;

    // Condition variable for waiting transactions
    std::condition_variable cv;

    // Set of transactions currently waiting for locks
    std::set<int> waitingTransactions;

    // Timeout for waiting transactions
    const std::chrono::milliseconds lockTimeout{5000}; // 5 second

    // Resource allocation graph for deadlock detection
    ResourceAllocationGraph &rag;

    /**
     * @brief Checks if a lock request is compatible with currently granted locks
     * @param resourceId ID of the resource
     * @param request The lock request to check
     * @return true if compatible, false otherwise
     */
    bool isCompatible(int resourceId, const LockRequest &request) const;

    /**
     * @brief Gets all transactions that currently hold locks on a resource
     * @param resourceId ID of the resource
     * @return Vector of transaction IDs
     */
    std::vector<int> getLockHolders(int resourceId) const;

public:
    /**
     * @brief Constructs a new LockManager
     * @param logger Reference to the logger for recording operations
     * @param rag Reference to the resource allocation graph
     */
    explicit LockManager(Logger &logger, ResourceAllocationGraph &rag);

    /**
     * @brief Attempts to acquire a lock on behalf of a transaction
     * @param txnId ID of the transaction requesting the lock
     * @param resourceId ID of the resource to lock
     * @param lockType Type of lock requested
     * @param wait Whether to wait if lock cannot be immediately granted
     * @return true if lock was acquired, false otherwise
     */
    bool acquireLock(int txnId, int resourceId, LockType lockType, bool wait = true);

    /**
     * @brief Releases a lock held by a transaction
     * @param txnId ID of the transaction releasing the lock
     * @param resourceId ID of the resource to unlock
     * @return true if lock was released, false if transaction didn't hold the lock
     */
    bool releaseLock(int txnId, int resourceId);

    /**
     * @brief Releases all locks held by a transaction (e.g., after commit/abort)
     * @param txnId ID of the transaction
     */
    void releaseAllLocks(int txnId);

    /**
     * @brief Checks if a transaction holds any lock on a resource
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @return true if transaction holds a lock, false otherwise
     */
    bool holdsLock(int txnId, int resourceId) const;

    /**
     * @brief Gets the type of lock held by a transaction on a resource
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @return Lock type if held, or nullptr if no lock is held
     */
    LockType *getLockType(int txnId, int resourceId) const;

    /**
     * @brief Gets all resources locked by a transaction
     * @param txnId ID of the transaction
     * @return Set of resource IDs
     */
    std::set<int> getResourcesLockedBy(int txnId) const;

    /**
     * @brief Gets all transactions waiting for a lock on a resource
     * @param resourceId ID of the resource
     * @return Vector of transaction IDs
     */
    std::vector<int> getWaitingTransactions(int resourceId) const;

    /**
     * @brief Upgrades a shared lock to an exclusive lock
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @param wait Whether to wait if upgrade cannot be immediately granted
     * @return true if upgrade was successful, false otherwise
     */
    bool upgradeLock(int txnId, int resourceId, bool wait = true);

    /**
     * @brief Check for a deadlock in the system
     * @param deadlockCycle Output parameter that will contain the cycle if a deadlock is found
     * @return true if a deadlock is found, false otherwise
     */
    bool detectDeadlock(std::vector<int> &deadlockCycle);

    /**
     * @brief Get a string representation of the resource allocation graph
     * @return String representation of the graph
     */
    std::string getResourceAllocationGraph() const;

    bool holdsLockInternal(int txnId, int resourceId) const;
    LockType *getLockTypeInternal(int txnId, int resourceId) const;
    std::vector<int> getLockHoldersInternal(int resourceId) const;
    std::set<int> getResourcesLockedByInternal(int txnId) const;
    std::vector<int> getWaitingTransactionsInternal(int resourceId) const;
    bool upgradeLockInternal(int txnId, int resourceId, bool wait);

    // Helper methods for condition variables
    void notifyWaitingTransactions();
    bool waitForLock(int txnId, int resourceId, LockType lockType);
    bool tryGrantLock(int resourceId, LockRequest &request);
};