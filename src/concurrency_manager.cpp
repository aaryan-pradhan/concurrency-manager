#include "../include/concurrency_manager.h"
#include <sstream>
#include <algorithm>
#include <limits>

ConcurrencyManager::ConcurrencyManager(const std::string& logFilePath)
    : logger(logFilePath, true),
      rag(logger),
      lockManager(logger, rag), 
      nextTxnId(1) {
    logger.info("Concurrency Manager initialized with Two-Phase Locking protocol and RAG");
}

ConcurrencyManager::~ConcurrencyManager() {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Abort any active transactions on shutdown
    std::vector<int> activeTxns;
    for (const auto& pair : transactions) {
        if (pair.second->getState() != TransactionState::COMMITTED && 
            pair.second->getState() != TransactionState::ABORTED) {
            activeTxns.push_back(pair.first);
        }
    }
    
    // Release the mutex before calling abortTransaction to avoid deadlock
    lock.~lock_guard();
    
    for (int txnId : activeTxns) {
        abortTransaction(txnId, "System shutdown");
    }
    
    logger.info("Concurrency Manager shutdown");
}

Transaction* ConcurrencyManager::getTransaction(int txnId) {
    auto it = transactions.find(txnId);
    if (it != transactions.end()) {
        return it->second.get();
    }
    return nullptr;
}

int ConcurrencyManager::beginTransaction(const std::string& metadata) {
    std::lock_guard<std::mutex> lock(mtx);
    
    int txnId = nextTxnId++;
    transactions[txnId] = std::make_unique<Transaction>(txnId, metadata);
    
    logger.logTransactionStart(txnId);
    return txnId;
}

bool ConcurrencyManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait) {
    // First check transaction state without holding the lock manager's mutex
    {
        std::lock_guard<std::mutex> lock(mtx);
        
        Transaction* txn = getTransaction(txnId);
        if (!txn) {
            logger.error("Cannot acquire lock: Transaction T" + std::to_string(txnId) + " not found");
            return false;
        }
        
        // In 2PL, locks can only be acquired in the growing phase
        if (!txn->isInGrowingPhase()) {
            logger.warning("Cannot acquire lock: Transaction T" + std::to_string(txnId) + 
                         " is not in growing phase");
            return false;
        }
    }
    
    // Try to acquire the lock through the lock manager
    // This may block if wait=true and the lock cannot be granted immediately
    bool acquired = lockManager.acquireLock(txnId, resourceId, lockType, wait);
    
    // If lock acquired, update the transaction's record
    if (acquired) {
        std::lock_guard<std::mutex> lock(mtx);
        
        Transaction* txn = getTransaction(txnId);
        if (txn) {
            txn->acquireLock(resourceId);
        } else {
            // This should not happen, but just in case
            lockManager.releaseLock(txnId, resourceId);
            logger.error("Transaction T" + std::to_string(txnId) + 
                       " disappeared during lock acquisition. Rolling back lock.");
            return false;
        }
    }
    
    return acquired;
}

bool ConcurrencyManager::releaseLock(int txnId, int resourceId) {
    std::unique_lock<std::mutex> lock(mtx);
    
    Transaction* txn = getTransaction(txnId);
    if (!txn) {
        logger.error("Cannot release lock: Transaction T" + std::to_string(txnId) + " not found");
        return false;
    }
    
    // In basic 2PL, releasing a lock transitions the transaction to SHRINKING phase
    // First, update the transaction's state
    if (!txn->releaseLock(resourceId)) {
        logger.error("Cannot release lock: Transaction T" + std::to_string(txnId) + 
                   " doesn't hold lock on R" + std::to_string(resourceId));
        return false;
    }
    
    // Release the concurrency manager mutex before calling lock manager
    // to avoid potential deadlocks
    lock.unlock();
    
    // Then, release the lock in the lock manager
    bool released = lockManager.releaseLock(txnId, resourceId);
    
    if (released) {
        lock.lock();
        logger.info("T" + std::to_string(txnId) + " released lock on R" + 
                   std::to_string(resourceId) + " (now in " + 
                   (txn->isInGrowingPhase() ? "GROWING" : "SHRINKING") + " phase)");
    }
    
    return released;
}

bool ConcurrencyManager::commitTransaction(int txnId) {
    std::unique_lock<std::mutex> lock(mtx);
    
    Transaction* txn = getTransaction(txnId);
    if (!txn) {
        logger.error("Cannot commit: Transaction T" + std::to_string(txnId) + " not found");
        return false;
    }
    
    // Update the transaction state
    if (!txn->commit()) {
        logger.error("Cannot commit: Transaction T" + std::to_string(txnId) + 
                   " is already committed or aborted");
        return false;
    }
    
    // Release the concurrency manager mutex before calling lock manager
    lock.unlock();
    
    // Release all locks held by the transaction
    lockManager.releaseAllLocks(txnId);
    
    logger.logTransactionCommit(txnId);
    return true;
}

bool ConcurrencyManager::abortTransaction(int txnId, const std::string& reason) {
    std::unique_lock<std::mutex> lock(mtx);
    
    Transaction* txn = getTransaction(txnId);
    if (!txn) {
        logger.error("Cannot abort: Transaction T" + std::to_string(txnId) + " not found");
        return false;
    }
    
    // Update the transaction state
    txn->abort();
    
    // Release the concurrency manager mutex before calling lock manager
    lock.unlock();
    
    // Release all locks held by the transaction
    lockManager.releaseAllLocks(txnId);
    
    logger.logTransactionAbort(txnId, reason);
    return true;
}

TransactionState ConcurrencyManager::getTransactionState(int txnId) {
    std::lock_guard<std::mutex> lock(mtx);
    
    Transaction* txn = getTransaction(txnId);
    if (!txn) {
        logger.warning("Transaction T" + std::to_string(txnId) + " not found");
        return TransactionState::ABORTED; // Return ABORTED for non-existent transactions
    }
    
    return txn->getState();
}

std::set<int> ConcurrencyManager::getLocksHeldBy(int txnId) {
    std::lock_guard<std::mutex> lock(mtx);
    
    Transaction* txn = getTransaction(txnId);
    if (!txn) {
        logger.warning("Transaction T" + std::to_string(txnId) + " not found");
        return {}; // Return empty set for non-existent transactions
    }
    
    // Release our mutex before calling into the lock manager
    lock.~lock_guard();
    
    return lockManager.getResourcesLockedBy(txnId);
}

std::string ConcurrencyManager::getSystemState() const {
    std::lock_guard<std::mutex> lock(mtx);
    
    std::stringstream ss;
    ss << "=== Concurrency Manager State ===\n";
    
    ss << "Active Transactions: " << transactions.size() << "\n";
    for (const auto& pair : transactions) {
        int txnId = pair.first;
        const Transaction* txn = pair.second.get();
        
        ss << "- T" << txnId << ": ";
        
        // Convert state enum to string
        switch (txn->getState()) {
            case TransactionState::GROWING:
                ss << "GROWING";
                break;
            case TransactionState::SHRINKING:
                ss << "SHRINKING";
                break;
            case TransactionState::COMMITTED:
                ss << "COMMITTED";
                break;
            case TransactionState::ABORTED:
                ss << "ABORTED";
                break;
        }
        
        // Show metadata if available
        if (!txn->getMetadata().empty()) {
            ss << " (" << txn->getMetadata() << ")";
        }
        
        // Show age
        ss << ", Age: " << txn->getAgeMillis() << "ms";
        
        // Show locks held
        const auto& locks = txn->getLocksHeld();
        if (!locks.empty()) {
            ss << ", Locks: ";
            bool first = true;
            for (int resourceId : locks) {
                if (!first) ss << ", ";
                ss << "R" << resourceId;
                first = false;
            }
        }
        
        ss << "\n";
    }
    
    return ss.str();
}

// Implement deadlock checking
bool ConcurrencyManager::checkForDeadlocks() {
    std::vector<int> deadlockCycle;
    
    // Log deadlock detection start
    logger.logDeadlockDetectionStart();
    
    // Check for deadlocks using the RAG
    bool foundDeadlock = lockManager.detectDeadlock(deadlockCycle);
    
    if (foundDeadlock) {
        // Log deadlock detection
        logger.logDeadlockDetected(deadlockCycle);
        
        // Choose a victim (youngest transaction in the cycle)
        int victimId = -1;
        long youngestAge = std::numeric_limits<long>::max();
        
        std::lock_guard<std::mutex> lock(mtx);
        for (int id : deadlockCycle) {
            // Skip resource IDs (they're negative in our convention)
            if (id < 0) continue;
            
            Transaction* txn = getTransaction(id);
            if (txn && txn->getAgeMillis() < youngestAge) {
                youngestAge = txn->getAgeMillis();
                victimId = id;
            }
        }
        
        // Abort the victim transaction
        if (victimId != -1) {
            logger.logDeadlockResolution(victimId);
            
            // Release lock before calling abortTransaction to avoid deadlock
            lock.~lock_guard();
            abortTransaction(victimId, "Deadlock resolution");
        }
        
        logger.logDeadlockDetectionComplete(true);
        return true;
    }
    
    logger.logDeadlockDetectionComplete(false);
    return false;
}

std::string ConcurrencyManager::getResourceAllocationGraph() const {
    return lockManager.getResourceAllocationGraph();
}

void ConcurrencyManager::logResourceAllocationGraph(const std::string& transactionInfo) const {
    rag.logToFile(transactionInfo);
}