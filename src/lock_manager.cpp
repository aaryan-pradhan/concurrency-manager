#include "../include/lock_manager.h"
#include "../include/transaction.h"
#include <algorithm>
#include <thread>

LockManager::LockManager(Logger& loggerRef, ResourceAllocationGraph& ragRef)
    : logger(loggerRef), rag(ragRef) {
    logger.info("Lock Manager initialized with RAG and condition variable support");
}

// Helper method to notify waiting transactions
void LockManager::notifyWaitingTransactions() {
    // Wake up all waiting transactions
    cv.notify_all();
}

// Modify waitForLock to update RAG
bool LockManager::waitForLock(int txnId, int resourceId, LockType lockType) {
    // Add to waiting set
    waitingTransactions.insert(txnId);
    
    // Create a predicate that checks if this transaction can get the lock OR has been aborted
    auto canAcquireLock = [this, txnId, resourceId, lockType]() {
        // Check if transaction has been aborted (by getting its current state)
        Transaction* txn = Transaction::GetTransaction(txnId);
        if (txn == nullptr || txn->getState() == TransactionState::ABORTED) {
            return true;  // Exit the wait if transaction is aborted
        }
        
        // If resource doesn't exist, it's available
        if (lockTable.find(resourceId) == lockTable.end()) {
            return true;
        }
        
        // Find this transaction's request
        const auto& requests = lockTable.at(resourceId);
        auto it = std::find_if(requests.begin(), requests.end(),
                              [txnId](const LockRequest& req) {
                                  return req.transactionId == txnId;
                              });
        
        // If request not found or already granted, no need to wait
        if (it == requests.end() || it->granted) {
            return true;
        }
        
        // Check if the request is compatible with current granted locks
        return isCompatible(resourceId, *it);
    };
    
    // Wait indefinitely until the condition is met or the transaction is aborted
    std::unique_lock<std::mutex> lock(mtx);
    bool success = cv.wait_for(lock, lockTimeout, canAcquireLock);    
    // Remove from waiting set
    waitingTransactions.erase(txnId);
    
    if (!success) {
        // Remove request edge from RAG
        rag.removeRequestEdge(txnId, resourceId);
        
        logger.warning("T" + std::to_string(txnId) + " timed out waiting for lock on R" + 
                     std::to_string(resourceId));
        
        // Find and remove the request
        if (lockTable.find(resourceId) != lockTable.end()) {
            auto& requests = lockTable[resourceId];
            auto it = std::find_if(requests.begin(), requests.end(),
                                  [txnId](const LockRequest& req) {
                                      return !req.granted && req.transactionId == txnId;
                                  });
            
            if (it != requests.end()) {
                requests.erase(it);
                if (requests.empty()) {
                    lockTable.erase(resourceId);
                }
            }
        }
        
        return false;
    }
    // Check if the transaction was aborted
    Transaction* txn = Transaction::GetTransaction(txnId);
    if (txn == nullptr || txn->getState() == TransactionState::ABORTED) {
        return false;  // Return false if transaction has been aborted
    }
    
    // Try to grant the lock
    if (lockTable.find(resourceId) != lockTable.end()) {
        auto& requests = lockTable[resourceId];
        auto it = std::find_if(requests.begin(), requests.end(),
                              [txnId](const LockRequest& req) {
                                  return !req.granted && req.transactionId == txnId;
                              });
        
        if (it != requests.end() && isCompatible(resourceId, *it)) {
            it->granted = true;
            
            // Update RAG: request edge becomes assignment edge
            rag.addAssignmentEdge(resourceId, txnId);
            rag.removeRequestEdge(txnId, resourceId);
            
            logger.info("T" + std::to_string(txnId) + " acquired " + 
                       lockTypeToString(it->type) + " lock on R" + 
                       std::to_string(resourceId));
            return true;
        }
    }
    
    return false;
}

// Helper method to try to grant a lock
bool LockManager::tryGrantLock(int resourceId, LockRequest& request) {
    if (isCompatible(resourceId, request)) {
        request.granted = true;
        logger.info("T" + std::to_string(request.transactionId) + " acquired " + 
                   lockTypeToString(request.type) + " lock on R" + 
                   std::to_string(resourceId));
        return true;
    }
    return false;
}

bool LockManager::isCompatible(int resourceId, const LockRequest& request) const {
    // If there are no existing locks, any request is compatible
    if (lockTable.find(resourceId) == lockTable.end()) {
        return true;
    }
    
    const auto& requests = lockTable.at(resourceId);
    
    // If request is for SHARED lock, it's compatible unless there's an EXCLUSIVE lock granted
    if (request.type == LockType::SHARED) {
        return std::none_of(requests.begin(), requests.end(), [](const LockRequest& r) {
            return r.granted && r.type == LockType::EXCLUSIVE;
        });
    }
    
    // If request is for EXCLUSIVE lock, it's only compatible if there are no other locks granted
    return std::none_of(requests.begin(), requests.end(), [&request](const LockRequest& r) {
        return r.granted && r.transactionId != request.transactionId;
    });
}

// ----- Internal methods (no mutex locking) -----

std::vector<int> LockManager::getLockHoldersInternal(int resourceId) const {
    std::vector<int> holders;
    
    if (lockTable.find(resourceId) == lockTable.end()) {
        return holders;
    }
    
    const auto& requests = lockTable.at(resourceId);
    for (const auto& req : requests) {
        if (req.granted) {
            holders.push_back(req.transactionId);
        }
    }
    
    return holders;
}

bool LockManager::holdsLockInternal(int txnId, int resourceId) const {
    if (lockTable.find(resourceId) == lockTable.end()) {
        return false;
    }
    
    const auto& requests = lockTable.at(resourceId);
    return std::any_of(requests.begin(), requests.end(), 
                      [txnId](const LockRequest& req) { 
                          return req.granted && req.transactionId == txnId; 
                      });
}

LockType* LockManager::getLockTypeInternal(int txnId, int resourceId) const {
    if (lockTable.find(resourceId) == lockTable.end()) {
        return nullptr;
    }
    
    const auto& requests = lockTable.at(resourceId);
    auto it = std::find_if(requests.begin(), requests.end(), 
                          [txnId](const LockRequest& req) { 
                              return req.granted && req.transactionId == txnId; 
                          });
    
    if (it == requests.end()) {
        return nullptr;
    }
    
    // Return pointer to the lock type
    return const_cast<LockType*>(&(it->type));
}

std::set<int> LockManager::getResourcesLockedByInternal(int txnId) const {
    std::set<int> resources;
    
    for (const auto& entry : lockTable) {
        int resourceId = entry.first;
        const auto& requests = entry.second;
        
        if (std::any_of(requests.begin(), requests.end(), 
                       [txnId](const LockRequest& req) { 
                           return req.granted && req.transactionId == txnId; 
                       })) {
            resources.insert(resourceId);
        }
    }
    
    return resources;
}

std::vector<int> LockManager::getWaitingTransactionsInternal(int resourceId) const {
    std::vector<int> waitingTxns;
    
    if (lockTable.find(resourceId) == lockTable.end()) {
        return waitingTxns;
    }
    
    const auto& requests = lockTable.at(resourceId);
    for (const auto& req : requests) {
        if (!req.granted) {
            waitingTxns.push_back(req.transactionId);
        }
    }
    
    return waitingTxns;
}

bool LockManager::upgradeLockInternal(int txnId, int resourceId, bool wait) {
    if (lockTable.find(resourceId) == lockTable.end()) {
        logger.warning("T" + std::to_string(txnId) + " attempted to upgrade lock on non-locked resource R" + 
                      std::to_string(resourceId));
        return false;
    }
    
    auto& requests = lockTable[resourceId];
    
    // Find the SHARED lock held by this transaction
    auto it = std::find_if(requests.begin(), requests.end(), 
                          [txnId](const LockRequest& req) { 
                              return req.granted && req.transactionId == txnId && 
                                     req.type == LockType::SHARED; 
                          });
    
    if (it == requests.end()) {
        logger.warning("T" + std::to_string(txnId) + " does not hold a SHARED lock to upgrade on R" + 
                      std::to_string(resourceId));
        return false;
    }
    
    // Check if upgrade is possible (no other granted locks except this txn's SHARED lock)
    bool canUpgrade = std::all_of(requests.begin(), requests.end(), 
                                 [txnId](const LockRequest& req) { 
                                     return !req.granted || req.transactionId == txnId; 
                                 });
    
    if (canUpgrade) {
        // Upgrade the lock
        it->type = LockType::EXCLUSIVE;
        logger.info("T" + std::to_string(txnId) + " upgraded lock to EXCLUSIVE on R" + 
                   std::to_string(resourceId));
        return true;
    } else if (!wait) {
        // Can't upgrade immediately and don't want to wait
        logger.warning("T" + std::to_string(txnId) + " lock upgrade denied (no wait) on R" + 
                      std::to_string(resourceId));
        return false;
    } else {
        // Create a new exclusive lock request that will wait
        // First, remove the shared lock
        requests.erase(it);
        
        // Then add a new exclusive lock request
        LockRequest newRequest(txnId, LockType::EXCLUSIVE);
        requests.push_back(newRequest);
        
        // Return false to indicate the lock hasn't been upgraded yet
        // The transaction will need to wait
        logger.info("T" + std::to_string(txnId) + " waiting to upgrade lock on R" + 
                   std::to_string(resourceId));
        return false;
    }
}

// ----- Public methods (with mutex locking) -----

std::vector<int> LockManager::getLockHolders(int resourceId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return getLockHoldersInternal(resourceId);
}

// Modify acquireLock to update RAG
bool LockManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait) {
    // Check if transaction is valid and not aborted
    Transaction* txn = Transaction::GetTransaction(txnId);
    if (txn == nullptr || txn->getState() == TransactionState::ABORTED) {
        logger.warning("T" + std::to_string(txnId) + " cannot acquire lock - transaction is aborted");
        return false;
    }

    logger.info("T" + std::to_string(txnId) + " attempting to acquire " + 
               lockTypeToString(lockType) + " lock on R" + std::to_string(resourceId));
    
    std::unique_lock<std::mutex> lock(mtx);
    
    // Check if transaction already holds this lock
    if (holdsLockInternal(txnId, resourceId)) {
        LockType* currentLockType = getLockTypeInternal(txnId, resourceId);
        
        // If it holds the same type or stronger, return success
        if (currentLockType && (*currentLockType == lockType || 
            (*currentLockType == LockType::EXCLUSIVE && lockType == LockType::SHARED))) {
            logger.info("T" + std::to_string(txnId) + " already holds appropriate lock on R" + 
                       std::to_string(resourceId));
            return true;
        }
        
        // If it holds SHARED and wants EXCLUSIVE, try to upgrade
        if (currentLockType && *currentLockType == LockType::SHARED && 
            lockType == LockType::EXCLUSIVE) {
            lock.unlock();  // Release the lock before waiting
            logger.info("T" + std::to_string(txnId) + " attempting to upgrade lock on R" + 
                       std::to_string(resourceId));
            return upgradeLock(txnId, resourceId, wait);
        }
    }
    
    // Create the lock request
    LockRequest newRequest(txnId, lockType);
    
    // Check if the lock can be granted immediately
    bool canGrantImmediately = isCompatible(resourceId, newRequest);
    
    if (canGrantImmediately) {
        // Grant the lock immediately
        newRequest.granted = true;
        lockTable[resourceId].push_back(newRequest);
        
        // Add assignment edge to RAG
        rag.addAssignmentEdge(resourceId, txnId);
        
        logger.info("T" + std::to_string(txnId) + " acquired " + 
                   lockTypeToString(lockType) + " lock on R" + std::to_string(resourceId));
        return true;
    } else if (!wait) {
        // Can't grant immediately and don't want to wait
        logger.warning("T" + std::to_string(txnId) + " lock request denied (no wait) on R" + 
                      std::to_string(resourceId));
        return false;
    } else {
        // Add request edge to RAG
        rag.addRequestEdge(txnId, resourceId);
        auto holders = getLockHoldersInternal(resourceId);
        if (!holders.empty()) {
            logger.info("T" + std::to_string(txnId) + " waiting for lock on R" + 
                       std::to_string(resourceId) + " held by T" + 
                       std::to_string(holders[0]));
        }
    
        // Add the request to the queue
        lockTable[resourceId].push_back(newRequest);
        
        // Get lock holders for logging
        if (!holders.empty()) {
            logger.info("T" + std::to_string(txnId) + " waiting for lock on R" + 
                       std::to_string(resourceId) + " held by T" + 
                       std::to_string(holders[0]));
        }
        
        // Release the lock before waiting to avoid deadlock
        lock.unlock();
        
        // Wait for the lock to be available
        return waitForLock(txnId, resourceId, lockType);
    }
}

// Modify releaseLock to update RAG
bool LockManager::releaseLock(int txnId, int resourceId) {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Check if resource exists in lock table
    if (lockTable.find(resourceId) == lockTable.end()) {
        logger.warning("T" + std::to_string(txnId) + " attempted to release lock on non-locked resource R" + 
                      std::to_string(resourceId));
        return false;
    }
    
    auto& requests = lockTable[resourceId];
    
    // Find the lock held by this transaction
    auto it = std::find_if(requests.begin(), requests.end(), 
                          [txnId](const LockRequest& req) { 
                              return req.granted && req.transactionId == txnId; 
                          });
    
    if (it == requests.end()) {
        logger.warning("T" + std::to_string(txnId) + " does not hold a lock on R" + 
                      std::to_string(resourceId));
        return false;
    }
    
    // Remove the lock
    requests.erase(it);
    
    // Remove assignment edge from RAG
    rag.removeAssignmentEdge(resourceId, txnId);
    
    // If no more lock requests for this resource, remove the resource entry
    if (requests.empty()) {
        lockTable.erase(resourceId);
    } else {
        // Try to grant locks to waiting transactions
        for (auto& req : requests) {
            if (!req.granted && isCompatible(resourceId, req)) {
                req.granted = true;
                logger.info("T" + std::to_string(req.transactionId) + " acquired " + 
                           lockTypeToString(req.type) + " lock on R" + std::to_string(resourceId));
            }
        }
    }
    
    logger.info("T" + std::to_string(txnId) + " released lock on R" + 
               std::to_string(resourceId));
    
    // Notify waiting transactions
    notifyWaitingTransactions();
    
    return true;
}

// Modify releaseAllLocks to update RAG
void LockManager::releaseAllLocks(int txnId) {
    std::lock_guard<std::mutex> lock(mtx);

    // CRITICAL FIX: First remove the transaction from all waiting queues
    // where it might be waiting for locks but hasn't acquired them yet
    for (auto& entry : lockTable) {
        auto& requests = entry.second;
        
        // Remove any pending (non-granted) requests from this transaction
        auto originalSize = requests.size();
        requests.erase(
            std::remove_if(requests.begin(), requests.end(), 
                         [txnId](const LockRequest& req) { 
                             return !req.granted && req.transactionId == txnId; 
                         }),
            requests.end()
        );
        
        // If we removed any waiting requests, log it
        if (requests.size() < originalSize) {
            logger.info("T" + std::to_string(txnId) + " removed from waiting queue for resource R" + 
                       std::to_string(entry.first));
        }
    }
    
    // Also explicitly remove from the waitingTransactions set
    if (waitingTransactions.erase(txnId) > 0) {
        logger.info("T" + std::to_string(txnId) + " removed from global waiting set");
    }
    
    std::set<int> resourcesToRelease;
    
    // Find all resources locked by this transaction
    for (const auto& entry : lockTable) {
        int resourceId = entry.first;
        for (const auto& req : entry.second) {
            if (req.granted && req.transactionId == txnId) {
                resourcesToRelease.insert(resourceId);
                break;
            }
        }
    }
    
    // Release each lock
    for (int resourceId : resourcesToRelease) {
        auto& requests = lockTable[resourceId];
        
        // Find and remove the lock held by this transaction
        auto it = std::find_if(requests.begin(), requests.end(), 
                              [txnId](const LockRequest& req) { 
                                  return req.granted && req.transactionId == txnId; 
                              });
        
        if (it != requests.end()) {
            requests.erase(it);
            logger.logLockReleased(txnId, resourceId);
            
            // Remove assignment edge from RAG
            rag.removeAssignmentEdge(resourceId, txnId);
            logger.info("T" + std::to_string(txnId) + " released lock on R" + 
                       std::to_string(resourceId));
            
            // If no more lock requests for this resource, remove the resource entry
            if (requests.empty()) {
                lockTable.erase(resourceId);
            } else {
                // Try to grant locks to waiting transactions
                for (auto& req : requests) {
                    if (!req.granted && isCompatible(resourceId, req)) {
                        req.granted = true;
                        logger.info("T" + std::to_string(req.transactionId) + " acquired " + 
                                   lockTypeToString(req.type) + " lock on R" + 
                                   std::to_string(resourceId));
                    }
                }
            }
        }
    }
    
    // Clear transaction from RAG
    rag.clearTransaction(txnId);
    
    if (!resourcesToRelease.empty()) {
        logger.info("Released all " + std::to_string(resourcesToRelease.size()) + 
                   " locks held by transaction T" + std::to_string(txnId));
        
        // Notify waiting transactions
        notifyWaitingTransactions();
    }
}

bool LockManager::holdsLock(int txnId, int resourceId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return holdsLockInternal(txnId, resourceId);
}

LockType* LockManager::getLockType(int txnId, int resourceId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return getLockTypeInternal(txnId, resourceId);
}

std::set<int> LockManager::getResourcesLockedBy(int txnId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return getResourcesLockedByInternal(txnId);
}

std::vector<int> LockManager::getWaitingTransactions(int resourceId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return getWaitingTransactionsInternal(resourceId);
}

bool LockManager::upgradeLock(int txnId, int resourceId, bool wait) {
    std::unique_lock<std::mutex> lock(mtx);

    // First try to upgrade immediately
    bool immediate = upgradeLockInternal(txnId, resourceId, true);
    
    if (immediate || !wait) {
        // Either upgraded successfully or don't want to wait
        return immediate;
    }
    
    // If we reach here, we need to wait for the upgrade
    // First, make sure we have a request in the queue (done by upgradeLockInternal)
    
    // Now we need to wait - release the lock before waiting
    lock.unlock();
    
    // Wait for the lock to be available
    return waitForLock(txnId, resourceId, LockType::EXCLUSIVE);
}

bool LockManager::detectDeadlock(std::vector<int>& deadlockCycle) {
    return rag.detectDeadlock(deadlockCycle);
}

std::string LockManager::getResourceAllocationGraph() const {
    return rag.toString();
}