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

    // If request is for EXCLUSIVE lock, it's only compatible if no *other* transaction holds a lock
    // (the requester's own SHARED lock does not block its upgrade)
    return std::none_of(requests.begin(), requests.end(), [&request](const LockRequest& r) {
        return r.granted && r.transactionId != request.transactionId;
    });
}

// ----- Internal methods (caller holds mtx) -----

bool LockManager::isAbortedInternal(int txnId) const {
    auto txn = Transaction::GetTransaction(txnId);
    return txn == nullptr || txn->getState() == TransactionState::ABORTED;
}

bool LockManager::holdsSufficientInternal(int txnId, int resourceId, LockType lockType) const {
    auto it = lockTable.find(resourceId);
    if (it == lockTable.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [txnId, lockType](const LockRequest& r) {
        return r.granted && r.transactionId == txnId &&
               (lockType == LockType::SHARED || r.type == LockType::EXCLUSIVE);
    });
}

long LockManager::pendingRequestIndexInternal(int txnId, int resourceId) const {
    auto it = lockTable.find(resourceId);
    if (it == lockTable.end()) {
        return -1;
    }
    const auto& requests = it->second;
    for (size_t i = 0; i < requests.size(); ++i) {
        if (!requests[i].granted && requests[i].transactionId == txnId) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

// Every grant of a waiting request goes through here, so the RAG and upgrade
// bookkeeping stay consistent no matter which thread performs the grant.
void LockManager::grantInternal(int resourceId, size_t index) {
    auto& requests = lockTable[resourceId];
    requests[index].granted = true;
    int txnId = requests[index].transactionId;
    LockType type = requests[index].type;

    if (type == LockType::EXCLUSIVE) {
        // Completing an upgrade: the transaction's SHARED entry is superseded
        for (size_t j = 0; j < requests.size(); ++j) {
            if (j != index && requests[j].granted && requests[j].transactionId == txnId) {
                requests.erase(requests.begin() + j);
                break;
            }
        }
    }

    // Request edge becomes an assignment edge
    rag.addAssignmentEdge(resourceId, txnId);

    logger.info("T" + std::to_string(txnId) + " acquired " +
               lockTypeToString(type) + " lock on R" + std::to_string(resourceId));
}

void LockManager::grantWaitersInternal(int resourceId) {
    bool granted = true;
    while (granted) {
        granted = false;
        auto it = lockTable.find(resourceId);
        if (it == lockTable.end()) {
            return;
        }
        auto& requests = it->second;

        // Pending upgrades first: they already hold SHARED, so granting them avoids
        // letting a new transaction write in between their read and their write
        for (size_t i = 0; i < requests.size() && !granted; ++i) {
            const auto& req = requests[i];
            bool isUpgrade = !req.granted && req.type == LockType::EXCLUSIVE &&
                             holdsLockInternal(req.transactionId, resourceId);
            if (isUpgrade && isCompatible(resourceId, req)) {
                grantInternal(resourceId, i);
                granted = true;
            }
        }
        for (size_t i = 0; i < requests.size() && !granted; ++i) {
            if (!requests[i].granted && isCompatible(resourceId, requests[i])) {
                grantInternal(resourceId, i);
                granted = true;
            }
        }
    }
}

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

LockManager::UpgradeResult LockManager::upgradeLockInternal(int txnId, int resourceId, bool wait) {
    if (lockTable.find(resourceId) == lockTable.end()) {
        logger.warning("T" + std::to_string(txnId) + " attempted to upgrade lock on non-locked resource R" +
                      std::to_string(resourceId));
        return UpgradeResult::DENIED;
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
        return UpgradeResult::DENIED;
    }

    // Check if upgrade is possible (no other granted locks except this txn's SHARED lock)
    bool canUpgrade = std::all_of(requests.begin(), requests.end(),
                                 [txnId](const LockRequest& req) {
                                     return !req.granted || req.transactionId == txnId;
                                 });

    if (canUpgrade) {
        // Upgrade the lock in place
        it->type = LockType::EXCLUSIVE;
        logger.info("T" + std::to_string(txnId) + " upgraded lock to EXCLUSIVE on R" +
                   std::to_string(resourceId));
        return UpgradeResult::GRANTED;
    }
    if (!wait) {
        // Can't upgrade immediately and don't want to wait
        logger.warning("T" + std::to_string(txnId) + " lock upgrade denied (no wait) on R" +
                      std::to_string(resourceId));
        return UpgradeResult::DENIED;
    }

    // Queue an EXCLUSIVE request but KEEP the granted SHARED lock. Giving it up here would
    // let another transaction write between this transaction's read and its write, which
    // breaks two-phase locking (a release followed by an acquisition) and serializability.
    if (pendingRequestIndexInternal(txnId, resourceId) < 0) {
        requests.push_back(LockRequest(txnId, LockType::EXCLUSIVE));
        rag.addRequestEdge(txnId, resourceId);
    }
    logger.info("T" + std::to_string(txnId) + " waiting to upgrade lock on R" +
               std::to_string(resourceId) + " (SHARED lock kept)");
    return UpgradeResult::QUEUED;
}

bool LockManager::waitForLock(int txnId, int resourceId, LockType lockType) {
    std::unique_lock<std::mutex> lock(mtx);
    waitingTransactions.insert(txnId);

    // Stop waiting when: the transaction was aborted, the request was already granted
    // by a releasing thread (no pending entry left), or the request is now grantable
    auto canProceed = [this, txnId, resourceId]() {
        if (isAbortedInternal(txnId)) {
            return true;
        }
        long index = pendingRequestIndexInternal(txnId, resourceId);
        if (index < 0) {
            return true;
        }
        return isCompatible(resourceId, lockTable[resourceId][static_cast<size_t>(index)]);
    };

    cv.wait(lock, canProceed);
    waitingTransactions.erase(txnId);

    long index = pendingRequestIndexInternal(txnId, resourceId);

    if (isAbortedInternal(txnId)) {
        if (index >= 0) {
            auto& requests = lockTable[resourceId];
            requests.erase(requests.begin() + index);
            if (requests.empty()) {
                lockTable.erase(resourceId);
            }
        }
        rag.removeRequestEdge(txnId, resourceId);
        logger.info("T" + std::to_string(txnId) + " aborted while waiting for lock on R" +
                   std::to_string(resourceId));
        return false;
    }

    if (index >= 0) {
        grantInternal(resourceId, static_cast<size_t>(index));
        return true;
    }

    // No pending entry: a releasing thread granted it while we slept
    return holdsSufficientInternal(txnId, resourceId, lockType);
}

// ----- Public methods (with mutex locking) -----

std::vector<int> LockManager::getLockHolders(int resourceId) const {
    std::lock_guard<std::mutex> lock(mtx);
    return getLockHoldersInternal(resourceId);
}

bool LockManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait) {
    std::unique_lock<std::mutex> lock(mtx);

    // Checked under mtx: the deadlock detector marks a victim ABORTED before it takes mtx to
    // release the victim's locks, so an aborted transaction can never be granted a lock afterwards.
    if (isAbortedInternal(txnId)) {
        logger.warning("T" + std::to_string(txnId) + " cannot acquire lock - transaction is aborted");
        return false;
    }

    logger.info("T" + std::to_string(txnId) + " attempting to acquire " +
               lockTypeToString(lockType) + " lock on R" + std::to_string(resourceId));

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

        // If it holds SHARED and wants EXCLUSIVE, upgrade
        logger.info("T" + std::to_string(txnId) + " attempting to upgrade lock on R" +
                   std::to_string(resourceId));
        UpgradeResult result = upgradeLockInternal(txnId, resourceId, wait);
        if (result != UpgradeResult::QUEUED) {
            return result == UpgradeResult::GRANTED;
        }
        lock.unlock();
        return waitForLock(txnId, resourceId, LockType::EXCLUSIVE);
    }

    // Create the lock request
    LockRequest newRequest(txnId, lockType);

    if (isCompatible(resourceId, newRequest)) {
        // Grant the lock immediately
        newRequest.granted = true;
        lockTable[resourceId].push_back(newRequest);

        // Add assignment edge to RAG
        rag.addAssignmentEdge(resourceId, txnId);

        logger.info("T" + std::to_string(txnId) + " acquired " +
                   lockTypeToString(lockType) + " lock on R" + std::to_string(resourceId));
        return true;
    }
    if (!wait) {
        // Can't grant immediately and don't want to wait
        logger.warning("T" + std::to_string(txnId) + " lock request denied (no wait) on R" +
                      std::to_string(resourceId));
        return false;
    }

    // Queue the request and add a request edge to the RAG
    rag.addRequestEdge(txnId, resourceId);
    auto holders = getLockHoldersInternal(resourceId);
    lockTable[resourceId].push_back(newRequest);
    if (!holders.empty()) {
        logger.info("T" + std::to_string(txnId) + " waiting for lock on R" +
                   std::to_string(resourceId) + " held by T" +
                   std::to_string(holders[0]));
    }

    // Release the mutex before waiting so other transactions and the detector can proceed
    lock.unlock();
    return waitForLock(txnId, resourceId, lockType);
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

    // Remove assignment edge from RAG
    rag.removeAssignmentEdge(resourceId, txnId);

    // If no more lock requests for this resource, remove the resource entry;
    // otherwise grant whatever has become compatible
    if (requests.empty()) {
        lockTable.erase(resourceId);
    } else {
        grantWaitersInternal(resourceId);
    }

    logger.info("T" + std::to_string(txnId) + " released lock on R" +
               std::to_string(resourceId));

    // Notify waiting transactions
    notifyWaitingTransactions();

    return true;
}

void LockManager::releaseAllLocks(int txnId) {
    std::lock_guard<std::mutex> lock(mtx);

    std::vector<int> touched;
    size_t released = 0;

    for (auto& entry : lockTable) {
        auto& requests = entry.second;
        auto originalSize = requests.size();
        size_t grantedBefore = static_cast<size_t>(std::count_if(requests.begin(), requests.end(),
            [txnId](const LockRequest& req) { return req.granted && req.transactionId == txnId; }));

        // Remove both waiting requests and granted locks of this transaction
        requests.erase(
            std::remove_if(requests.begin(), requests.end(),
                         [txnId](const LockRequest& req) { return req.transactionId == txnId; }),
            requests.end()
        );

        if (requests.size() < originalSize) {
            touched.push_back(entry.first);
            released += grantedBefore;
            if (grantedBefore > 0) {
                logger.info("T" + std::to_string(txnId) + " released lock on R" +
                           std::to_string(entry.first));
            } else {
                logger.info("T" + std::to_string(txnId) + " removed from waiting queue for resource R" +
                           std::to_string(entry.first));
            }
        }
    }

    waitingTransactions.erase(txnId);

    // Clear transaction from RAG before granting, so new assignment edges survive
    rag.clearTransaction(txnId);

    for (int resourceId : touched) {
        auto it = lockTable.find(resourceId);
        if (it == lockTable.end()) {
            continue;
        }
        if (it->second.empty()) {
            lockTable.erase(it);
        } else {
            grantWaitersInternal(resourceId);
        }
    }

    if (released > 0) {
        logger.info("Released all " + std::to_string(released) +
                   " locks held by transaction T" + std::to_string(txnId));
    }

    // Always notify: an aborted transaction may be waiting and must wake up to see it
    notifyWaitingTransactions();
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

std::vector<std::pair<int, int>> LockManager::getWaitForEdges() const {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::pair<int, int>> edges;
    for (const auto& entry : lockTable) {
        const auto& requests = entry.second;
        for (const auto& waiter : requests) {
            if (waiter.granted) {
                continue;
            }
            for (const auto& holder : requests) {
                bool bothShared = waiter.type == LockType::SHARED && holder.type == LockType::SHARED;
                if (holder.granted && holder.transactionId != waiter.transactionId && !bothShared) {
                    edges.emplace_back(waiter.transactionId, holder.transactionId);
                }
            }
        }
    }
    return edges;
}

bool LockManager::upgradeLock(int txnId, int resourceId, bool wait) {
    std::unique_lock<std::mutex> lock(mtx);
    UpgradeResult result = upgradeLockInternal(txnId, resourceId, wait);
    if (result != UpgradeResult::QUEUED) {
        return result == UpgradeResult::GRANTED;
    }
    lock.unlock();
    return waitForLock(txnId, resourceId, LockType::EXCLUSIVE);
}

bool LockManager::detectDeadlock(std::vector<int>& deadlockCycle) {
    return rag.detectDeadlock(deadlockCycle);
}

std::string LockManager::getResourceAllocationGraph() const {
    return rag.toString();
}
