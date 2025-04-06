#include "../include/concurrency_manager.h"
#include <sstream>
#include <iostream>

ConcurrencyManager::ConcurrencyManager(const std::string& logFilePath, uint64_t detectionIntervalMs)
    : logger(logFilePath), lockManager(logger), nextTxnId(1) {
    
    // Initialize deadlock detector with references to lock manager and logger
    deadlockDetector = std::make_unique<DeadlockDetector>(lockManager, logger, detectionIntervalMs);
    
    logger.info("Concurrency Manager initialized with deadlock detection interval: " + 
               std::to_string(detectionIntervalMs) + "ms");
}

ConcurrencyManager::~ConcurrencyManager() {
    // Destroy deadlock detector first (stops background thread)
    deadlockDetector.reset();
    
    // Cleanup remaining transactions
    std::vector<int> txnIds;
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& pair : transactions) {
            txnIds.push_back(pair.first);
        }
    }
    
    // Abort remaining transactions
    for (int id : txnIds) {
        abortTransaction(id, "System shutdown");
    }
    
    logger.info("Concurrency Manager shut down");
}

Transaction* ConcurrencyManager::getTransaction(int txnId) {
    auto it = transactions.find(txnId);
    if (it == transactions.end()) {
        return nullptr;
    }
    return it->second.get();
}

int ConcurrencyManager::beginTransaction(const std::string& metadata) {
    std::lock_guard<std::mutex> lock(mtx);
    
    int txnId = nextTxnId++;
    transactions[txnId] = std::make_unique<Transaction>(txnId, metadata);
    
    // Transaction constructor will register itself with the static registry
    
    logger.info("Transaction T" + std::to_string(txnId) + " began" + 
               (metadata.empty() ? "" : " with metadata: " + metadata));
    
    return txnId;
}

bool ConcurrencyManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait) {
    // Check if transaction exists
    Transaction* txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn) {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found while acquiring lock");
            return false;
        }
        
        // Check if transaction is in a valid state for acquiring locks
        if (txn->getState() != TransactionState::GROWING) {
            logger.warning("Transaction T" + std::to_string(txnId) + " not in GROWING phase, cannot acquire lock");
            return false;
        }
    }
    
    // Try to acquire lock
    bool acquired = lockManager.acquireLock(txnId, resourceId, lockType, wait);
    
    if (acquired) {
        // Update transaction's record of held locks
        txn->acquireLock(resourceId);
    }
    
    return acquired;
}

bool ConcurrencyManager::releaseLock(int txnId, int resourceId) {
    // Check if transaction exists
    Transaction* txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn) {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found while releasing lock");
            return false;
        }
    }
    
    // Release the lock
    bool released = lockManager.releaseLock(txnId, resourceId);
    
    if (released) {
        // Update transaction's record of held locks
        txn->releaseLock(resourceId);
    }
    
    return released;
}

bool ConcurrencyManager::commitTransaction(int txnId) {
    Transaction* txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn) {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found during commit");
            return false;
        }
    }
    
    // Try to commit the transaction
    bool committed = txn->commit();
    
    if (committed) {
        logger.info("Transaction T" + std::to_string(txnId) + " committed successfully");
        
        // Release all locks
        lockManager.releaseAllLocks(txnId);
        
        // Remove transaction from active map
        std::lock_guard<std::mutex> lock(mtx);
        transactions.erase(txnId);
    }
    
    return committed;
}

bool ConcurrencyManager::abortTransaction(int txnId, const std::string& reason) {
    Transaction* txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn) {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found during abort");
            return false;
        }
    }
    
    // Abort the transaction
    txn->abort();
    
    logger.info("Transaction T" + std::to_string(txnId) + " aborted: " + 
               (reason.empty() ? "User initiated" : reason));
    
    // Release all locks
    lockManager.releaseAllLocks(txnId);
    
    // Remove transaction from active map
    std::lock_guard<std::mutex> lock(mtx);
    transactions.erase(txnId);
    
    return true;
}

TransactionState ConcurrencyManager::getTransactionState(int txnId) {
    std::lock_guard<std::mutex> lock(mtx);
    
    Transaction* txn = getTransaction(txnId);
    if (!txn) {
        return TransactionState::ABORTED; // Default to aborted if not found
    }
    
    return txn->getState();
}

std::set<int> ConcurrencyManager::getLocksHeldBy(int txnId) {
    return lockManager.getResourcesLockedBy(txnId);
}

std::string ConcurrencyManager::getSystemState() const {
    std::stringstream ss;
    std::lock_guard<std::mutex> lock(mtx);
    
    ss << "Concurrency Manager State:" << std::endl;
    ss << "Active Transactions: " << transactions.size() << std::endl;
    
    for (const auto& pair : transactions) {
        const Transaction* txn = pair.second.get();
        ss << "Transaction " << txn->getId() << ": " 
           << (txn->getState() == TransactionState::GROWING ? "GROWING" : 
              txn->getState() == TransactionState::SHRINKING ? "SHRINKING" : 
              txn->getState() == TransactionState::COMMITTED ? "COMMITTED" : "ABORTED")
           << ", Age: " << txn->getAgeMillis() << "ms";
        
        if (!txn->getMetadata().empty()) {
            ss << ", Metadata: " << txn->getMetadata();
        }
        
        ss << std::endl;
    }
    
    // Get wait-for graph from deadlock detector
    auto edgeList = deadlockDetector->GetEdgeList();
    if (!edgeList.empty()) {
        ss << "Current Wait-For Graph:" << std::endl;
        for (const auto& edge : edgeList) {
            ss << "T" << edge.first << " -> T" << edge.second << std::endl;
        }
    }
    
    return ss.str();
}