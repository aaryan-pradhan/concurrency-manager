#include "../include/transaction.h"
#include <unordered_map>

Transaction::Transaction(int id, const std::string& meta,int priority)
    : txnId(id),
      state(TransactionState::GROWING),
      startTime(std::chrono::system_clock::now()),
      metadata(meta),
      priority(priority) {
    // Registration is done by the owner (ConcurrencyManager) once the object is
    // held by a shared_ptr, so lookups can never observe a destroyed transaction.
}

bool Transaction::acquireLock(int resourceId) {
    // In 2PL, locks can only be acquired in the GROWING phase
    if (state.load() != TransactionState::GROWING) {
        return false;
    }

    locksHeld.insert(resourceId);
    return true;
}

bool Transaction::releaseLock(int resourceId) {
    // Check if the transaction actually holds this lock
    if (locksHeld.find(resourceId) == locksHeld.end()) {
        return false;
    }

    // Releasing any lock ends the growing phase (basic 2PL)
    beginShrinking();

    TransactionState s = state.load();
    if (s != TransactionState::COMMITTED &&
        s != TransactionState::ABORTED &&
        s != TransactionState::SHRINKING) {
        return false;
    }

    locksHeld.erase(resourceId);
    return true;
}

bool Transaction::commit() {
    // Can only commit from GROWING or SHRINKING; compare-and-swap so a concurrent
    // abort by the deadlock detector and a commit cannot both succeed
    TransactionState s = state.load();
    while (s == TransactionState::GROWING || s == TransactionState::SHRINKING) {
        if (state.compare_exchange_weak(s, TransactionState::COMMITTED)) {
            return true;
        }
    }
    return false;
}

bool Transaction::abort() {
    TransactionState s = state.load();
    while (s != TransactionState::COMMITTED) {
        if (s == TransactionState::ABORTED || state.compare_exchange_weak(s, TransactionState::ABORTED)) {
            return true;
        }
    }
    return false;
}

bool Transaction::beginShrinking() {
    TransactionState expected = TransactionState::GROWING;
    return state.compare_exchange_strong(expected, TransactionState::SHRINKING);
}

bool Transaction::isInGrowingPhase() const {
    return state.load() == TransactionState::GROWING;
}

TransactionState Transaction::getState() const {
    return state.load();
}

int Transaction::getId() const {
    return txnId;
}

const std::set<int>& Transaction::getLocksHeld() const {
    return locksHeld;
}

std::chrono::system_clock::time_point Transaction::getStartTime() const {
    return startTime;
}

const std::string& Transaction::getMetadata() const {
    return metadata;
}

bool Transaction::hasLock(int resourceId) const {
    return locksHeld.find(resourceId) != locksHeld.end();
}

long Transaction::getAgeMillis() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - startTime).count();
}

// Initialize the static registry
std::unordered_map<txn_id_t, std::shared_ptr<Transaction>> Transaction::active_transactions_;
std::mutex Transaction::registry_mutex_;

// Register a new transaction
void Transaction::RegisterTransaction(const std::shared_ptr<Transaction>& txn) {
    if (txn != nullptr) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        active_transactions_[txn->getId()] = txn;
    }
}

// Remove a transaction from the registry
void Transaction::UnregisterTransaction(txn_id_t txn_id, const Transaction* expected) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = active_transactions_.find(txn_id);
    if (it != active_transactions_.end() && (expected == nullptr || it->second.get() == expected)) {
        active_transactions_.erase(it);
    }
}

// Get a transaction by ID
std::shared_ptr<Transaction> Transaction::GetTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = active_transactions_.find(txn_id);
    if (it != active_transactions_.end()) {
        return it->second;
    }
    return nullptr;
}

int Transaction::getPriority() const {
    return priority;
}
