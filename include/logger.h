#pragma once

#include <fstream>
#include <string>
#include <mutex>
#include <vector>
#include <chrono>
#include <iomanip>
#include <iostream>

/**
 * @enum LogLevel
 * @brief Defines the severity levels for logging
 */
enum class LogLevel {
    DEBUG,    // Detailed information, useful for debugging
    INFO,     // General information about system operation
    WARNING,  // Potential issues that don't prevent system operation
    ERROR,    // Issues that might prevent parts of the system from functioning
    FATAL     // Critical issues that prevent the system from functioning
};

/**
 * @class Logger
 * @brief Thread-safe logging facility for the concurrency manager
 */
class Logger {
private:
    std::ofstream logFile;       // File stream for persistent logging
    std::mutex mtx;              // Mutex for thread safety
    bool consoleOutput;          // Whether to output logs to console
    LogLevel minLevel;           // Minimum level to log
    
    /**
     * @brief Gets current timestamp as a formatted string
     * @return Formatted timestamp string
     */
    std::string getTimestamp() const;
    
    /**
     * @brief Log a message with the specified level
     * @param level Severity level of the message
     * @param message The message to log
     */
    void log(LogLevel level, const std::string& message);

public:
    /**
     * @brief Constructs a new Logger object
     * @param filename Path to the log file
     * @param toConsole Whether to also output logs to console
     * @param level Minimum level of messages to log
     */
    Logger(const std::string& filename, bool toConsole = true, LogLevel level = LogLevel::INFO);
    
    /**
     * @brief Destructor - ensures log file is properly closed
     */
    ~Logger();

    // Transaction lifecycle logging
    void logTransactionStart(int txnId);
    void logTransactionCommit(int txnId);
    void logTransactionAbort(int txnId, const std::string& reason = "");

    // Lock operations logging
    void logLockAcquireAttempt(int txnId, int resourceId, const std::string& lockType);
    void logLockAcquired(int txnId, int resourceId, const std::string& lockType);
    void logLockReleased(int txnId, int resourceId);
    void logLockWaiting(int txnId, int resourceId, int holdingTxnId);
    
    // Deadlock detection logging
    void logDeadlockDetectionStart();
    void logDeadlockDetected(const std::vector<int>& cycle);
    void logDeadlockResolution(int victimTxnId);
    void logDeadlockDetectionComplete(bool foundDeadlock);
    
    // Tree protocol specific logging
    void logTreeLockRequest(int txnId, int resourceId);
    void logTreeLockViolation(int txnId, int resourceId, int violationReason);
    
    // General logging methods
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);
    
    /**
     * @brief Set the minimum log level
     * @param level The minimum level of messages to log
     */
    void setLogLevel(LogLevel level);
    
    /**
     * @brief Enable or disable console output
     * @param enable Whether to enable console output
     */
    void setConsoleOutput(bool enable);
};