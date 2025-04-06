# Two-Phase Locking (2PL) Protocol - Concurrency Control System

This project implements a concurrency control system for database management systems using the **Two-Phase Locking (2PL) protocol**. It provides a robust framework for managing transaction isolation in multi-user database environments.

## Table of Contents
- [Overview](#overview)
- [Components](#components)
  - [Core Classes](#core-classes)
  - [Supporting Components](#supporting-components)
- [Implementation Details](#implementation-details)
  - [Two-Phase Locking Protocol](#two-phase-locking-protocol)
  - [Lock Waiting Mechanism](#lock-waiting-mechanism)
- [Building and Running](#building-and-running)
  - [Prerequisites](#prerequisites)
  - [Compilation](#compilation)
  - [Running Tests](#running-tests)
- [Test Cases](#test-cases)
- [Features](#features)

## Overview

This implementation provides:

- Basic **Two-Phase Locking (2PL)** protocol (not strict 2PL) for transaction isolation.
- Lock compatibility management for **shared** and **exclusive locks**.
- Multi-threaded transaction execution.
- Transaction state tracking, enforcing **growing** and **shrinking phases**.
- Deadlock prevention using timeouts.
- A comprehensive logging system for debugging and monitoring.

## Components

### Core Classes

1. **LockManager**
   - Manages lock acquisition, compatibility checking, and queueing.
   - Handles both shared and exclusive locks.
   - Implements waiting for locks using condition variables.
   - Provides methods for lock upgrades and bulk releases.

2. **ConcurrencyManager**
   - Implements the Two-Phase Locking protocol.
   - Manages transaction lifecycles (begin, commit, abort).
   - Enforces 2PL phase transitions (growing → shrinking).
   - Delegates lock operations to the `LockManager`.

3. **Transaction**
   - Represents a database transaction.
   - Tracks locks held by the transaction.
   - Maintains transaction state (`GROWING`, `SHRINKING`, `COMMITTED`, `ABORTED`).
   - Enforces phase transition rules.

4. **Logger**
   - Thread-safe logging facility.
   - Records transaction and lock operations.
   - Supports multiple log levels.
   - Outputs logs to both file and console.

### Supporting Components

1. **2PL Test Runner**
   - Multi-threaded test framework for transaction scenarios.

2. **Test Files**
   - Predefined transaction schedules to validate correctness.

## Implementation Details

### Two-Phase Locking Protocol

The implementation adheres to the basic Two-Phase Locking protocol:

1. **Growing Phase**:
   - Transactions can acquire locks but cannot release any locks.
   - All lock acquisitions must happen in this phase.

2. **Shrinking Phase**:
   - Begins when a transaction releases its first lock.
   - Transactions can release locks but cannot acquire new locks.

These rules are enforced in `Transaction::releaseLock()` and `ConcurrencyManager::acquireLock()`.

#### Lock Types:
- **SHARED**: Multiple transactions can hold shared locks on the same resource.
- **EXCLUSIVE**: Only one transaction can hold an exclusive lock on a resource.

### Lock Waiting Mechanism

Lock waiting is implemented using C++ condition variables:

1. When a lock cannot be granted immediately:
   - The request is added to the queue.
   - The transaction waits on a condition variable with a timeout.
   
2. Other transactions notify the condition variable upon lock release.

#### Timeout Prevention:
- A 5-second timeout prevents indefinite waiting.
- Failed acquisitions are properly cleaned up.

## Building and Running

### Prerequisites
- C++17 compatible compiler.
- Standard library with threading support.

### Compilation
Use your preferred build system or compiler to compile the project. For example:
``` bash
g++ -std=c++17 main.cpp LockManager.cpp ConcurrencyManager.cpp Transaction.cpp Logger.cpp -o 2pl_system
g++ -std=c++17 -o deadlock_test tests/test_deadlock_detection.cpp src/concurrency_manager.cpp src/lock_manager.cpp src/deadlock_detector.cpp src/transaction.cpp src/logger.cpp -pthread
```

### Running Tests
Run predefined test cases to validate the implementation:
``` bash
./2pl_system < tests/test1.txt
./2pl_system < tests/test2.txt
./2pl_system < tests/test3.txt
./deadlock_test < tests/test4.txt
```

## Test Cases

The repository includes several test cases:

1. **Basic Lock Operations (`test1.txt`)**:
   - Tests shared and exclusive lock acquisition.
   - Demonstrates lock compatibility rules.
   - Tests transaction commit operations.

2. **2PL Phase Transitions (`test2.txt`)**:
   - Demonstrates growing to shrinking phase transitions.
   - Tests restrictions on acquiring locks during shrinking phase.

3. **Lock Conflicts (`test3.txt`)**:
   - Tests how lock conflicts between transactions are handled.
   - Demonstrates waiting for locks and deadlock prevention.
  
  
## Features

- **Thread Safety**: All operations are thread-safe, protected by mutexes.
- **Deadlock Prevention**: Timeouts prevent indefinite waiting on locks.
- **Performance Optimization**: Internal methods avoid redundant mutex locking for efficiency.
- **Detailed Logging**: Comprehensive logging of all operations for debugging purposes.
- **State Monitoring**: Methods to query system state for debugging or analysis.
- **Clean Shutdown**: Automatically aborts active transactions during system shutdown.

---

This project demonstrates fundamental concepts in database transaction management and concurrency control, focusing on implementing the Two-Phase Locking protocol to maintain isolation between concurrent transactions effectively.
