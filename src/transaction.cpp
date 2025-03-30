#include "../include/transaction.h"

Transaction::Transaction(int id, const std::string& meta)
    : txnId(id), 
      state(TransactionState::GROWING), 
      startTime(std::chrono::system_clock::now()), 
      metadata(meta) {
}

bool Transaction::acquireLock(int resourceId) {
    // In 2PL, locks can only be acquired in the GROWING phase
    if (state != TransactionState::GROWING) {
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
    
    // In strict 2PL we only release locks after commit/abort,
    // but we implement the general case here
    if (state == TransactionState::GROWING) {
        // Auto-transition to SHRINKING phase
        beginShrinking();
    }
    
    if (state != TransactionState::COMMITTED && 
        state != TransactionState::ABORTED && 
        state != TransactionState::SHRINKING) {
        return false;
    }
    
    locksHeld.erase(resourceId);
    return true;
}

bool Transaction::commit() {
    // Can only commit if not already committed or aborted
    if (state == TransactionState::COMMITTED || state == TransactionState::ABORTED) {
        return false;
    }
    
    state = TransactionState::COMMITTED;
    return true;
}

void Transaction::abort() {
    state = TransactionState::ABORTED;
}

bool Transaction::beginShrinking() {
    if (state == TransactionState::GROWING) {
        state = TransactionState::SHRINKING;
        return true;
    }
    return false;  // Can't transition to SHRINKING from non-GROWING states
}

bool Transaction::isInGrowingPhase() const {
    return state == TransactionState::GROWING;
}

TransactionState Transaction::getState() const {
    return state;
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