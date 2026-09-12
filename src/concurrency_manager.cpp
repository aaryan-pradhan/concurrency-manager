#include "../include/concurrency_manager.h"
#include <sstream>
#include <iostream>
#include <limits>

ConcurrencyManager::ConcurrencyManager(const std::string &logFilePath, uint64_t detectionIntervalMs, LogLevel logLevel)
    : logger(logFilePath, false, logLevel),
      rag(logger),
      lockManager(logger, rag),
      nextTxnId(1)
{
    // Initialize deadlock detector

    deadlockDetector = std::make_unique<DeadlockDetector>(lockManager, logger, detectionIntervalMs);

    logger.info("Concurrency Manager initialized with Two-Phase Locking protocol and RAG");
    logger.info("Concurrency Manager initialized with deadlock detection interval: " +
                std::to_string(detectionIntervalMs) + "ms");
}

ConcurrencyManager::~ConcurrencyManager()
{
    // Destroy deadlock detector first (stops background thread)
    deadlockDetector.reset();

    // Cleanup remaining transactions
    std::vector<int> txnIds;
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto &pair : transactions)
        {
            txnIds.push_back(pair.first);
        }
    }

    // Abort remaining transactions
    for (int id : txnIds)
    {
        abortTransaction(id, "System shutdown");
    }

    logger.info("Concurrency Manager shut down");
}

std::shared_ptr<Transaction> ConcurrencyManager::getTransaction(int txnId)
{
    auto it = transactions.find(txnId);
    if (it == transactions.end())
    {
        return nullptr;
    }
    return it->second;
}

int ConcurrencyManager::beginTransaction(const std::string &metadata, int requestedTxnId, int priority)
{
    std::lock_guard<std::mutex> lock(mtx);

    int txnId;
    bool isRestart = false;

    if (requestedTxnId == -1)
    {
        // New transaction - assign next available ID
        txnId = nextTxnId++;
    }
    else
    {
        // Reusing existing transaction ID
        txnId = requestedTxnId;
        isRestart = true;

        // Check if the transaction ID is already in use
        if (transactions.find(txnId) != transactions.end())
        {
            // Transaction already exists
            auto txn = getTransaction(txnId);

            // Check if it's in a state we can work with
            if (txn && txn->getState() == TransactionState::ABORTED)
            {
                // Remove the existing aborted transaction. Release anything still recorded
                // under this ID first: an aborted incarnation must not hand locks to the new one.
                lockManager.releaseAllLocks(txnId);
                Transaction::UnregisterTransaction(txnId, txn.get());
                transactions.erase(txnId);
                logger.info("Replacing aborted transaction T" + std::to_string(txnId));
            }
            else
            {
                // Existing transaction is active or committed, can't reuse the ID
                logger.warning("Transaction T" + std::to_string(txnId) +
                               " already exists and is not aborted, cannot restart with same ID");
                return -1;
            }
        }
    }

    // Create the transaction object with specified priority
    auto txn = std::make_shared<Transaction>(txnId, metadata, priority);
    transactions[txnId] = txn;
    Transaction::RegisterTransaction(txn);

    // Log appropriate message based on whether this is a new or restarted transaction
    if (isRestart)
    {
        logger.info("Transaction T" + std::to_string(txnId) + " restarted" +
                    (metadata.empty() ? "" : " with metadata: " + metadata) +
                    " (priority: " + std::to_string(priority) + ")");
    }
    else
    {
        logger.info("Transaction T" + std::to_string(txnId) + " began" +
                    (metadata.empty() ? "" : " with metadata: " + metadata) +
                    " (priority: " + std::to_string(priority) + ")");
    }

    return txnId;
}

bool ConcurrencyManager::acquireLock(int txnId, int resourceId, LockType lockType, bool wait)
{
    // Check if transaction exists
    std::shared_ptr<Transaction> txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn)
        {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found while acquiring lock");
            return false;
        }

        // Check if transaction is in a valid state for acquiring locks
        if (txn->getState() != TransactionState::GROWING)
        {
            logger.warning("Transaction T" + std::to_string(txnId) + " not in GROWING phase, cannot acquire lock");
            return false;
        }
    }

    // Try to acquire lock
    bool acquired = lockManager.acquireLock(txnId, resourceId, lockType, wait);

    if (acquired)
    {
        // Update transaction's record of held locks
        txn->acquireLock(resourceId);
    }

    return acquired;
}

bool ConcurrencyManager::releaseLock(int txnId, int resourceId)
{
    // Check if transaction exists
    std::shared_ptr<Transaction> txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn)
        {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found while releasing lock");
            return false;
        }
    }

    // Release the lock
    bool released = lockManager.releaseLock(txnId, resourceId);

    if (released)
    {
        // Update transaction's record of held locks
        txn->releaseLock(resourceId);
    }

    return released;
}

bool ConcurrencyManager::commitTransaction(int txnId, const std::function<void()> &beforeRelease)
{
    std::shared_ptr<Transaction> txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn)
        {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found during commit");
            return false;
        }
    }

    // Commit point: atomic GROWING/SHRINKING -> COMMITTED (fails if the detector aborted it)
    bool committed = txn->commit();

    if (committed)
    {
        logger.info("Transaction T" + std::to_string(txnId) + " committed successfully");

        // Install effects while every lock is still held (strict 2PL)
        if (beforeRelease)
        {
            beforeRelease();
        }

        // Release all locks
        lockManager.releaseAllLocks(txnId);

        // Remove transaction from active map
        std::lock_guard<std::mutex> lock(mtx);
        Transaction::UnregisterTransaction(txnId, txn.get());
        transactions.erase(txnId);
    }

    return committed;
}

bool ConcurrencyManager::abortTransaction(int txnId, const std::string &reason)
{
    std::shared_ptr<Transaction> txn;
    {
        std::lock_guard<std::mutex> lock(mtx);
        txn = getTransaction(txnId);
        if (!txn)
        {
            logger.warning("Transaction T" + std::to_string(txnId) + " not found during abort");
            return false;
        }
    }

    // Abort the transaction (fails only if it already committed)
    if (!txn->abort())
    {
        logger.warning("Transaction T" + std::to_string(txnId) + " already committed, cannot abort");
        return false;
    }

    logger.info("Transaction T" + std::to_string(txnId) + " aborted: " +
                (reason.empty() ? "User initiated" : reason));

    // Release all locks
    lockManager.releaseAllLocks(txnId);

    // Remove transaction from active map
    std::lock_guard<std::mutex> lock(mtx);
    Transaction::UnregisterTransaction(txnId, txn.get());
    transactions.erase(txnId);

    return true;
}

TransactionState ConcurrencyManager::getTransactionState(int txnId)
{
    std::lock_guard<std::mutex> lock(mtx);

    auto txn = getTransaction(txnId);
    if (!txn)
    {
        return TransactionState::ABORTED; // Default to aborted if not found
    }

    return txn->getState();
}

std::set<int> ConcurrencyManager::getLocksHeldBy(int txnId)
{
    return lockManager.getResourcesLockedBy(txnId);
}

std::string ConcurrencyManager::getSystemState() const
{
    std::stringstream ss;
    std::lock_guard<std::mutex> lock(mtx);

    ss << "Concurrency Manager State:" << std::endl;
    ss << "Active Transactions: " << transactions.size() << std::endl;

    for (const auto &pair : transactions)
    {
        const Transaction *txn = pair.second.get();

        ss << "- T" << txn->getId() << ": ";

        // Convert state enum to string
        switch (txn->getState())
        {
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
        if (!txn->getMetadata().empty())
        {
            ss << ", Metadata: " << txn->getMetadata();
        }

        ss << std::endl;
    }

    // Get wait-for graph from deadlock detector
    auto edgeList = deadlockDetector->GetEdgeList();
    if (!edgeList.empty())
    {
        ss << "Current Wait-For Graph:" << std::endl;
        for (const auto &edge : edgeList)
        {
            ss << "T" << edge.first << " -> T" << edge.second << std::endl;
        }
    }

    return ss.str();
}

bool ConcurrencyManager::checkForDeadlocks()
{
    return deadlockDetector->RunCycleDetection() > 0;
}

std::string ConcurrencyManager::getResourceAllocationGraph() const
{
    return lockManager.getResourceAllocationGraph();
}

void ConcurrencyManager::logResourceAllocationGraph(const std::string &transactionInfo) const
{
    rag.logToFile(transactionInfo);
}
