#pragma once

#include <unordered_map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <set>
#include <string>
#include "logger.h"

// Lock types supported
enum class LockType {
    SHARED,
    EXCLUSIVE
};

// Convert LockType to string for logging
inline std::string lockTypeToString(LockType type) {
    return type == LockType::SHARED ? "SHARED" : "EXCLUSIVE";
}

// Structure to represent a lock request
struct LockRequest {
    int transactionId;
    LockType type;
    bool granted;
    
    LockRequest(int txnId, LockType lockType)
        : transactionId(txnId), type(lockType), granted(false) {}
};

class LockManager
{
private:
    std::unordered_map<int, std::vector<LockRequest>> lockTable;
    mutable std::mutex mtx;
    Logger &logger;
    std::condition_variable cv;

    // Set of transactions currently waiting for locks
    std::set<int> waitingTransactions;

    /**
     * @brief Checks if a lock request is compatible with currently granted locks
     * @param resourceId ID of the resource
     * @param request The lock request to check
     * @return true if compatible, false otherwise
     */
    bool isCompatible(int resourceId, const LockRequest &request) const;

public:
    /**
     * @brief Gets all transactions that currently hold locks on a resource
     * @param resourceId ID of the resource
     * @return Vector of transaction IDs
     */
    std::vector<int> getLockHolders(int resourceId) const;

    /**
     * @brief Constructs a new LockManager
     * @param logger Reference to the logger for recording operations
     */
    explicit LockManager(Logger &logger);

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
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @return true if lock was released, false otherwise
     */
    bool releaseLock(int txnId, int resourceId);
    
    /**
     * @brief Releases all locks held by a transaction
     * @param txnId ID of the transaction
     */
    void releaseAllLocks(int txnId);
    
    /**
     * @brief Checks if a transaction holds a lock on a resource
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @return true if transaction holds a lock, false otherwise
     */
    bool holdsLock(int txnId, int resourceId) const;
    
    /**
     * @brief Gets the type of lock a transaction holds on a resource
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @return Pointer to the lock type, or nullptr if transaction does not hold a lock
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
     * @brief Upgrades a SHARED lock to an EXCLUSIVE lock
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     * @param wait Whether to wait if upgrade cannot be immediately granted
     * @return true if lock was upgraded, false otherwise
     */
    bool upgradeLock(int txnId, int resourceId, bool wait = true);

    // Internal helper methods
    bool holdsLockInternal(int txnId, int resourceId) const;
    LockType *getLockTypeInternal(int txnId, int resourceId) const;
    std::vector<int> getLockHoldersInternal(int resourceId) const;
    std::set<int> getResourcesLockedByInternal(int txnId) const;
    std::vector<int> getWaitingTransactionsInternal(int resourceId) const;
    bool upgradeLockInternal(int txnId, int resourceId, bool wait = true);
    void notifyWaitingTransactions();
    bool waitForLock(int txnId, int resourceId, LockType lockType);
    bool tryGrantLock(int resourceId, LockRequest &request);
};