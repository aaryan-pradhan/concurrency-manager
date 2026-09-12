// Regression tests for the concurrency bugs fixed in the lock manager and deadlock detector.
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include "../include/concurrency_manager.h"

static int failures = 0;

static void check(bool ok, const std::string &name) {
    std::cout << (ok ? "PASSED: " : "FAILED: ") << name << std::endl;
    if (!ok) failures++;
}

static std::shared_ptr<Transaction> registerTxn(int id, int priority = 1) {
    auto txn = std::make_shared<Transaction>(id, "", priority);
    Transaction::RegisterTransaction(txn);
    return txn;
}

// A waiting upgrade must keep its SHARED lock, so no other transaction can write in between
static void upgradeKeepsSharedLock() {
    Logger logger("test_correctness.log", false);
    ResourceAllocationGraph rag(logger);
    LockManager lm(logger, rag);
    auto t1 = registerTxn(101), t2 = registerTxn(102), t3 = registerTxn(103);

    lm.acquireLock(101, 1, LockType::SHARED);
    lm.acquireLock(102, 1, LockType::SHARED);
    auto upgrade = std::async(std::launch::async, [&] { return lm.acquireLock(101, 1, LockType::EXCLUSIVE); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    check(lm.holdsLock(101, 1), "upgrader still holds SHARED while waiting");
    lm.releaseLock(102, 1);
    check(!lm.acquireLock(103, 1, LockType::EXCLUSIVE, false), "no other transaction gets EXCLUSIVE in the upgrade window");
    check(upgrade.get(), "upgrade is granted once the other reader leaves");
    LockType *type = lm.getLockType(101, 1);
    check(type && *type == LockType::EXCLUSIVE, "upgrader ends with a single EXCLUSIVE lock");
    lm.releaseAllLocks(101);
    Transaction::UnregisterTransaction(101);
    Transaction::UnregisterTransaction(102);
    Transaction::UnregisterTransaction(103);
}

// A lock granted to a waiter by the releasing thread must be reported as acquired
static void grantedWaitReturnsTrue() {
    Logger logger("test_correctness.log", false);
    ResourceAllocationGraph rag(logger);
    LockManager lm(logger, rag);
    auto t1 = registerTxn(201), t2 = registerTxn(202);

    lm.acquireLock(201, 7, LockType::EXCLUSIVE);
    auto waiter = std::async(std::launch::async, [&] { return lm.acquireLock(202, 7, LockType::SHARED); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    lm.releaseLock(201, 7);
    check(waiter.get(), "waiter granted by releaseLock gets true");
    check(lm.holdsLock(202, 7), "waiter holds the lock");
    lm.releaseAllLocks(202);
    Transaction::UnregisterTransaction(201);
    Transaction::UnregisterTransaction(202);
}

// The victim must be a member of the cycle, not a transaction merely waiting on it
static void victimIsCycleMember() {
    Logger logger("test_correctness.log", false);
    ResourceAllocationGraph rag(logger);
    LockManager lm(logger, rag);
    DeadlockDetector dd(lm, logger, 100000000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto a = registerTxn(1), b = registerTxn(2), c = registerTxn(3);
    dd.AddEdge(1, 2);
    dd.AddEdge(2, 3);
    dd.AddEdge(3, 2);
    txn_id_t victim = 0;
    check(dd.HasCycle(victim), "cycle T2<->T3 detected");
    check(victim == 2 || victim == 3, "victim is in the cycle (got T" + std::to_string(victim) + ")");

    Transaction::UnregisterTransaction(1);
    Transaction::UnregisterTransaction(2);
    Transaction::UnregisterTransaction(3);
    auto low = registerTxn(1, 0), mid = registerTxn(2, 5), high = registerTxn(3, 1);
    victim = 0;
    dd.HasCycle(victim);
    check(victim == 3, "lower-priority tail T1 is not chosen; lowest-priority cycle member T3 is");
    Transaction::UnregisterTransaction(1);
    Transaction::UnregisterTransaction(2);
    Transaction::UnregisterTransaction(3);
}

// End to end: a real two-transaction deadlock is broken and the survivor commits
static void deadlockResolvedEndToEnd() {
    ConcurrencyManager cm("test_correctness.log", 20, LogLevel::WARNING);
    int t1 = cm.beginTransaction("t1");
    int t2 = cm.beginTransaction("t2");
    cm.acquireLock(t1, 1, LockType::EXCLUSIVE);
    cm.acquireLock(t2, 2, LockType::EXCLUSIVE);
    auto f1 = std::async(std::launch::async, [&] { return cm.acquireLock(t1, 2, LockType::EXCLUSIVE); });
    auto f2 = std::async(std::launch::async, [&] { return cm.acquireLock(t2, 1, LockType::EXCLUSIVE); });
    bool r1 = f1.get(), r2 = f2.get();
    check(r1 != r2, "exactly one side of the deadlock is aborted");
    int survivor = r1 ? t1 : t2;
    check(cm.commitTransaction(survivor), "survivor commits");
    int victim = r1 ? t2 : t1;
    check(cm.getTransactionState(victim) == TransactionState::ABORTED, "victim is ABORTED");
}

// Commit and abort cannot both win
static void commitAbortExclusive() {
    Transaction t(900);
    check(t.abort(), "abort succeeds on a growing transaction");
    check(!t.commit(), "commit fails after abort");
    Transaction u(901);
    check(u.commit(), "commit succeeds on a growing transaction");
    check(!u.abort(), "abort fails after commit");
}

int main() {
    upgradeKeepsSharedLock();
    grantedWaitReturnsTrue();
    victimIsCycleMember();
    deadlockResolvedEndToEnd();
    commitAbortExclusive();
    std::cout << (failures == 0 ? "ALL PASSED" : std::to_string(failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
