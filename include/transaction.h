#pragma once

#include <set>
#include <vector>
#include <chrono>
#include <string>
#include <iostream>
#include <unordered_map>

using txn_id_t = uint64_t;

/**
 * @enum TransactionState
 * @brief Represents the current state of a transaction in the Two-Phase Locking protocol
 */
enum class TransactionState
{
    GROWING,   // Initial phase where locks can be acquired but not released
    SHRINKING, // Phase where locks can be released but not acquired
    COMMITTED, // Transaction has successfully committed
    ABORTED    // Transaction has been aborted
};

/**
 * @class Transaction
 * @brief Represents a database transaction with 2PL compliance
 */
class Transaction
{
private:
    int txnId;                                       // Unique transaction identifier
    TransactionState state;                          // Current state of the transaction
    std::set<int> locksHeld;                         // Set of resource IDs for which this transaction holds locks
    std::chrono::system_clock::time_point startTime; // Transaction start timestamp
    std::string metadata;                            // Optional transaction metadata
    int priority;                                    // Transaction priority for deadlock resolution

public:
    /**
     * @brief Constructs a new Transaction object
     * @param id Unique identifier for this transaction
     * @param meta Optional metadata for this transaction
     */
    Transaction(int id, const std::string &meta = "", int priority = 1);

    /**
     * @brief Destructor - unregisters the transaction from the static map
     */
    ~Transaction();

    /**
     * @brief Records acquisition of a lock on a resource
     * @param resourceId ID of the resource being locked
     * @return true if the lock was recorded successfully, false if transaction is not in GROWING phase
     */
    bool acquireLock(int resourceId);

    /**
     * @brief Records release of a lock on a resource
     * @param resourceId ID of the resource being unlocked
     * @return true if the lock was released successfully
     */
    bool releaseLock(int resourceId);

    /**
     * @brief Marks the transaction as committed
     * @return true if the transition to COMMITTED was successful
     */
    bool commit();

    /**
     * @brief Marks the transaction as aborted
     */
    void abort();

    /**
     * @brief Transitions the transaction from GROWING to SHRINKING phase
     * @return true if the transition was successful
     */
    bool beginShrinking();

    /**
     * @brief Checks if transaction is in growing phase
     * @return true if in GROWING phase, false otherwise
     */
    bool isInGrowingPhase() const;

    /**
     * @brief Gets the current state of the transaction
     * @return Current TransactionState
     */
    TransactionState getState() const;

    /**
     * @brief Gets the transaction ID
     * @return Transaction ID
     */
    int getId() const;

    /**
     * @brief Gets all resources locked by this transaction
     * @return Set of resource IDs
     */
    const std::set<int> &getLocksHeld() const;

    /**
     * @brief Gets the transaction's start time
     * @return Time point when transaction started
     */
    std::chrono::system_clock::time_point getStartTime() const;

    /**
     * @brief Gets transaction metadata
     * @return Metadata string
     */
    const std::string &getMetadata() const;

    /**
     * @brief Checks if transaction holds a lock on specified resource
     * @param resourceId Resource to check
     * @return true if transaction holds lock, false otherwise
     */
    bool hasLock(int resourceId) const;

    /**
     * @brief Gets transaction age in milliseconds
     * @return Age of transaction
     */
    long getAgeMillis() const;

    // Static map to store all active transactions by ID
    static std::unordered_map<txn_id_t, Transaction *> active_transactions_;

    // Register a transaction in the static map (call in constructor)
    static void RegisterTransaction(Transaction *txn);

    // Remove a transaction from the static map (call in destructor)
    static void UnregisterTransaction(txn_id_t txn_id);

    // Get a transaction by ID
    static Transaction *GetTransaction(txn_id_t txn_id);

    /**
     * @brief Gets the transaction priority
     * @return Transaction priority value
     */
    int getPriority() const;
};