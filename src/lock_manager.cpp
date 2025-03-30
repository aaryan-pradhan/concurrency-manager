#include "../include/lock_manager.h"
#include <algorithm>
#include <thread>

LockManager::LockManager(Logger& loggerRef)
    : logger(loggerRef) {
    logger.info("Lock Manager initialized with condition variable support");
}

// Helper method to notify waiting transactions
void LockManager::notifyWaitingTransactions() {
    // Wake up all waiting transactions
    cv.notify_all();
}

// Helper method to wait for a lock
bool LockManager::waitForLock(int txnId, int resourceId, LockType lockType) {
    // Add to waiting set
    waitingTransactions.insert(txnId);
    
    // Create a predicate that checks if this transaction can get the lock
    auto canAcquireLock = [this, txnId, resourceId, lockType]() {
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
    
    // Wait with timeout
    std::unique_lock<std::mutex> lock(mtx);
    bool success = cv.wait_for(lock, lockTimeout, canAcquireLock);
    
    // Remove from waiting set
    waitingTransactions.erase(txnId);
    
    if (!success) {
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
    
    // Try to grant the lock
    if (lockTable.find(resourceId) != lockTable.end()) {
        auto& requests = lockTable[resourceId];
        auto it = std::find_if(requests.begin(), requests.end(),
                              [txnId](const LockRequest& req) {
                                  return !req.granted && req.transactionId == txnId;
                              });
        
        if (it != requests.end() && isCompatible(resourceId, *it)) {
            it->granted = true;
            logger.logLockAcquired(txnId, resourceId, lockTypeToString(it->type));
            return true;
        }
    }
    
    return false;
}

// Helper method to try to grant a lock
bool LockManager::tryGrantLock(int resourceId, LockRequest& request) {
    if (isCompatible(resourceId, request)) {
        request.granted = true;
        logger.logLockAcquired(request.transactionId, resourceId, lockTypeToString(request.type));
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
        logger.logLockAcquired(txnId, resourceId, "EXCLUSIVE (upgraded)");
        return true;
    } else if (!wait) {
        // Can't upgrade immediately and don't want to wait
        logger.warning("T" + std::to_string(txnId) + " lock upgrade denied (no wait) on R" + 
                      std::to_string(resourceId));
        return false;
    } else {
        // Would need to wait for upgrade
        logger.warning("T" + std::to_string(txnId) + " would need to wait for lock upgrade on R" + 
                      std::to_string(resourceId));
        return false;
    }
}

// ----- Public methods (with mutex locking) -----

std::vector<int> LockManager::getLockHolders(int resourceId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return getLockHoldersInternal(resourceId);
}

bool LockManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait) {
    logger.logLockAcquireAttempt(txnId, resourceId, lockTypeToString(lockType));
    
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
            return upgradeLockInternal(txnId, resourceId, wait);
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
        
        logger.logLockAcquired(txnId, resourceId, lockTypeToString(lockType));
        return true;
    } else if (!wait) {
        // Can't grant immediately and don't want to wait
        logger.warning("T" + std::to_string(txnId) + " lock request denied (no wait) on R" + 
                      std::to_string(resourceId));
        return false;
    } else {
        // Add request to queue and wait
        auto holders = getLockHoldersInternal(resourceId);
        if (!holders.empty()) {
            logger.logLockWaiting(txnId, resourceId, holders[0]);
        }
        
        // Add the request to the queue
        lockTable[resourceId].push_back(newRequest);
        
        // Release the lock before waiting to avoid deadlock
        lock.unlock();
        
        // Wait for the lock to be available
        return waitForLock(txnId, resourceId, lockType);
    }
}

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
    
    // If no more lock requests for this resource, remove the resource entry
    if (requests.empty()) {
        lockTable.erase(resourceId);
    } else {
        // Try to grant locks to waiting transactions
        for (auto& req : requests) {
            if (!req.granted && isCompatible(resourceId, req)) {
                req.granted = true;
                logger.logLockAcquired(req.transactionId, resourceId, lockTypeToString(req.type));
            }
        }
    }
    
    logger.logLockReleased(txnId, resourceId);
    
    // Notify waiting transactions
    notifyWaitingTransactions();
    
    return true;
}

void LockManager::releaseAllLocks(int txnId) {
    std::lock_guard<std::mutex> lock(mtx);
    
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
            
            // If no more lock requests for this resource, remove the resource entry
            if (requests.empty()) {
                lockTable.erase(resourceId);
            } else {
                // Try to grant locks to waiting transactions
                for (auto& req : requests) {
                    if (!req.granted && isCompatible(resourceId, req)) {
                        req.granted = true;
                        logger.logLockAcquired(req.transactionId, resourceId, lockTypeToString(req.type));
                    }
                }
            }
        }
    }
    
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
    std::lock_guard<std::mutex> lock(mtx);
    return upgradeLockInternal(txnId, resourceId, wait);
}