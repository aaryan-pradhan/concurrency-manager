#include <iostream>
#include "../include/lock_manager.h"
#include "../include/logger.h"

// Helper function to print test results
void printTestResult(const std::string& testName, bool passed) {
    std::cout << testName << ": " << (passed ? "PASSED" : "FAILED") << std::endl;
}

// Test basic lock acquisition and release
void testBasicLockOperations(LockManager& lockManager, Logger& logger) {
    std::cout << "\n=== Testing Basic Lock Operations ===\n";
    
    // Acquire a shared lock
    bool result1 = lockManager.acquireLock(1, 100, LockType::SHARED);
    printTestResult("Acquire shared lock", result1);
    
    // Acquire another shared lock on the same resource by a different transaction
    bool result2 = lockManager.acquireLock(2, 100, LockType::SHARED);
    printTestResult("Acquire second shared lock on same resource", result2);
    
    // Try to acquire an exclusive lock on a resource that has shared locks
    bool result3 = lockManager.acquireLock(3, 100, LockType::EXCLUSIVE, false);
    printTestResult("Exclusive lock should fail when shared locks exist", !result3);
    
    // Release the shared locks
    bool result4 = lockManager.releaseLock(1, 100);
    bool result5 = lockManager.releaseLock(2, 100);
    printTestResult("Release shared locks", result4 && result5);
    
    // Now the exclusive lock should succeed
    bool result6 = lockManager.acquireLock(3, 100, LockType::EXCLUSIVE);
    printTestResult("Exclusive lock after releasing shared locks", result6);
    
    // Another exclusive lock should fail
    bool result7 = lockManager.acquireLock(4, 100, LockType::EXCLUSIVE, false);
    printTestResult("Second exclusive lock should fail", !result7);
    
    // Release the exclusive lock
    bool result8 = lockManager.releaseLock(3, 100);
    printTestResult("Release exclusive lock", result8);
}

// Test lock compatibility rules
void testLockCompatibility(LockManager& lockManager, Logger& logger) {
    std::cout << "\n=== Testing Lock Compatibility ===\n";
    
    // Clean up any existing locks
    lockManager.releaseAllLocks(10);
    lockManager.releaseAllLocks(11);
    lockManager.releaseAllLocks(12);
    
    // Shared locks are compatible with each other
    bool result1 = lockManager.acquireLock(10, 200, LockType::SHARED);
    bool result2 = lockManager.acquireLock(11, 200, LockType::SHARED);
    printTestResult("Multiple shared locks are compatible", result1 && result2);
    
    // Exclusive locks are not compatible with any other lock
    bool result3 = lockManager.acquireLock(12, 200, LockType::EXCLUSIVE, false);
    printTestResult("Exclusive lock incompatible with shared", !result3);
    
    // Release shared locks
    lockManager.releaseAllLocks(10);
    lockManager.releaseAllLocks(11);
    
    // Now exclusive lock should work
    bool result4 = lockManager.acquireLock(12, 200, LockType::EXCLUSIVE);
    printTestResult("Exclusive lock works after shared locks released", result4);
    
    // Shared lock should fail when exclusive lock exists
    bool result5 = lockManager.acquireLock(10, 200, LockType::SHARED, false);
    printTestResult("Shared lock incompatible with existing exclusive", !result5);
    
    // Clean up
    lockManager.releaseAllLocks(12);
}

// Test upgrade from shared to exclusive lock
void testLockUpgrade(LockManager& lockManager, Logger& logger) {
    std::cout << "\n=== Testing Lock Upgrade ===\n";
    
    // Acquire a shared lock
    bool result1 = lockManager.acquireLock(20, 300, LockType::SHARED);
    printTestResult("Acquire initial shared lock", result1);
    
    // Upgrade to exclusive should succeed when only this transaction has locks
    bool result2 = lockManager.upgradeLock(20, 300);
    printTestResult("Upgrade to exclusive succeeds when alone", result2);
    
    // Release and reset
    lockManager.releaseAllLocks(20);
    lockManager.releaseAllLocks(21);
    
    // Now create a scenario where upgrade should fail
    bool result3 = lockManager.acquireLock(20, 300, LockType::SHARED);
    bool result4 = lockManager.acquireLock(21, 300, LockType::SHARED);
    printTestResult("Setup multiple shared locks", result3 && result4);
    
    // Upgrade should fail because another transaction has a shared lock
    bool result5 = lockManager.upgradeLock(20, 300, false);
    printTestResult("Upgrade fails when another transaction has lock", !result5);
    
    // Clean up
    lockManager.releaseAllLocks(20);
    lockManager.releaseAllLocks(21);
}

// Test strict 2PL compliance
void testTwoPhaseLocking(LockManager& lockManager, Logger& logger) {
    std::cout << "\n=== Testing Two-Phase Locking Protocol ===\n";
    
    // Set up a test transaction
    int txnId = 50;
    
    // Growing phase - should be able to acquire locks
    bool result1 = lockManager.acquireLock(txnId, 600, LockType::SHARED);
    bool result2 = lockManager.acquireLock(txnId, 601, LockType::EXCLUSIVE);
    printTestResult("Growing phase - acquire locks", result1 && result2);
    
    // In strict 2PL, locks are held until commit or abort
    // This would be enforced by the ConcurrencyManager, not the LockManager
    logger.info("Note: Strict 2PL enforcement is handled by ConcurrencyManager");
    
    // We can verify that locks can be released
    bool result3 = lockManager.releaseLock(txnId, 600);
    printTestResult("Lock can be released", result3);
    
    // Clean up
    lockManager.releaseAllLocks(txnId);
}

// Test resource tracking and lock information retrieval
void testLockInformation(LockManager& lockManager, Logger& logger) {
    std::cout << "\n=== Testing Lock Information Retrieval ===\n";
    
    // Clean up from previous tests
    int txnId = 60;
    lockManager.releaseAllLocks(txnId);
    
    // Set up locks
    lockManager.acquireLock(txnId, 700, LockType::SHARED);
    lockManager.acquireLock(txnId, 701, LockType::EXCLUSIVE);
    lockManager.acquireLock(txnId, 702, LockType::SHARED);
    
    // Test holdsLock
    bool result1 = lockManager.holdsLock(txnId, 700);
    bool result2 = lockManager.holdsLock(txnId, 703);  // Should be false
    printTestResult("holdsLock correctly identifies locks", result1 && !result2);
    
    // Test getLockType
    LockType* lockType1 = lockManager.getLockType(txnId, 700);
    LockType* lockType2 = lockManager.getLockType(txnId, 701);
    printTestResult("getLockType returns correct lock types", 
                 lockType1 && lockType2 && 
                 *lockType1 == LockType::SHARED && 
                 *lockType2 == LockType::EXCLUSIVE);
    
    // Test getResourcesLockedBy
    auto resources = lockManager.getResourcesLockedBy(txnId);
    printTestResult("getResourcesLockedBy returns correct count", 
                 resources.size() == 3 && 
                 resources.find(700) != resources.end() &&
                 resources.find(701) != resources.end() &&
                 resources.find(702) != resources.end());
    
    // Clean up
    lockManager.releaseAllLocks(txnId);
}

int main() {
    // Initialize logger and lock manager
    Logger logger("test_lock_manager.log", true, LogLevel::DEBUG);
    LockManager lockManager(logger);
    
    std::cout << "Starting Lock Manager Tests\n";
    logger.info("Lock Manager test suite started");
    
    // Run tests
    testBasicLockOperations(lockManager, logger);
    testLockCompatibility(lockManager, logger);
    testLockUpgrade(lockManager, logger);
    testTwoPhaseLocking(lockManager, logger);
    testLockInformation(lockManager, logger);
    
    std::cout << "\nLock Manager tests completed.\n";
    logger.info("Lock Manager test suite completed");
    
    return 0;
}