#include "../include/logger.h"
#include <sstream>

// Convert LogLevel to string for readable output
std::string logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

Logger::Logger(const std::string& filename, bool toConsole, LogLevel level)
    : consoleOutput(toConsole), minLevel(level) {
    logFile.open(filename, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
    // Write a new line to separate logs from previous runs
    logFile << std::endl;
    logFile << "==================== New Log Session ====================" << std::endl;
    info("Logging started");
}

Logger::~Logger() {
    if (logFile.is_open()) {
        info("Logging ended");
        logFile.close();
    }
}

std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
        
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < minLevel) return;
    
    std::lock_guard<std::mutex> lock(mtx);
    
    std::string formattedMsg = getTimestamp() + " [" + logLevelToString(level) + "] " + message;
    
    if (logFile.is_open()) {
        logFile << formattedMsg << std::endl;
        logFile.flush();
    }
    
    if (consoleOutput) {
        // Colorize console output based on log level
        switch (level) {
            case LogLevel::DEBUG:
                std::cout << "\033[90m"; // Dark gray
                break;
            case LogLevel::INFO:
                std::cout << "\033[0m"; // Default color
                break;
            case LogLevel::WARNING:
                std::cout << "\033[33m"; // Yellow
                break;
            case LogLevel::ERROR:
                std::cout << "\033[31m"; // Red
                break;
            case LogLevel::FATAL:
                std::cout << "\033[91m"; // Bright red
                break;
        }
        
        std::cout << formattedMsg << "\033[0m" << std::endl;
    }
}

// Transaction lifecycle logging
void Logger::logTransactionStart(int txnId) {
    debug("Transaction T" + std::to_string(txnId) + " started");
}

void Logger::logTransactionCommit(int txnId) {
    info("Transaction T" + std::to_string(txnId) + " committed successfully");
}

void Logger::logTransactionAbort(int txnId, const std::string& reason) {
    std::string msg = "Transaction T" + std::to_string(txnId) + " aborted";
    if (!reason.empty()) {
        msg += " - Reason: " + reason;
    }
    warning(msg);
}

// Lock operations logging
void Logger::logLockAcquireAttempt(int txnId, int resourceId, const std::string& lockType) {
    debug("T" + std::to_string(txnId) + " attempting to acquire " + 
          lockType + " lock on resource R" + std::to_string(resourceId));
}

void Logger::logLockAcquired(int txnId, int resourceId, const std::string& lockType) {
    info("T" + std::to_string(txnId) + " acquired " + lockType + 
         " lock on resource R" + std::to_string(resourceId));
}

void Logger::logLockReleased(int txnId, int resourceId) {
    info("T" + std::to_string(txnId) + " released lock on resource R" + 
         std::to_string(resourceId));
}

void Logger::logLockWaiting(int txnId, int resourceId, int holdingTxnId) {
    debug("T" + std::to_string(txnId) + " waiting for lock on resource R" + 
          std::to_string(resourceId) + " held by T" + std::to_string(holdingTxnId));
}

// Deadlock detection logging
void Logger::logDeadlockDetectionStart() {
    debug("Starting deadlock detection");
}

void Logger::logDeadlockDetected(const std::vector<int>& cycle) {
    std::stringstream ss;
    ss << "Deadlock detected involving transactions: ";
    for (size_t i = 0; i < cycle.size(); ++i) {
        ss << "T" << cycle[i];
        if (i < cycle.size() - 1) {
            ss << " → ";
        }
    }
    warning(ss.str());
}

void Logger::logDeadlockResolution(int victimTxnId) {
    info("Resolving deadlock by aborting transaction T" + std::to_string(victimTxnId));
}

void Logger::logDeadlockDetectionComplete(bool foundDeadlock) {
    if (foundDeadlock) {
        debug("Deadlock detection completed: deadlocks found and resolved");
    } else {
        debug("Deadlock detection completed: no deadlocks found");
    }
}

// Tree protocol specific logging
void Logger::logTreeLockRequest(int txnId, int resourceId) {
    debug("T" + std::to_string(txnId) + " requesting tree lock on resource R" + 
          std::to_string(resourceId));
}

void Logger::logTreeLockViolation(int txnId, int resourceId, int violationReason) {
    std::string reason;
    if (violationReason == 1) {
        reason = "parent not locked";
    } else if (violationReason == 2) {
        reason = "sibling already locked";
    } else {
        reason = "protocol violation";
    }
    
    warning("T" + std::to_string(txnId) + " tree lock violation on resource R" + 
            std::to_string(resourceId) + ": " + reason);
}

// General logging methods
void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::fatal(const std::string& message) {
    log(LogLevel::FATAL, message);
}

// These log after updating, without holding mtx: log() takes mtx itself, and
// std::mutex is not recursive, so locking here first would deadlock the caller.
void Logger::setLogLevel(LogLevel level) {
    minLevel = level;
    info("Log level set to " + logLevelToString(level));
}

void Logger::setConsoleOutput(bool enable) {
    consoleOutput = enable;
    info(std::string("Console output ") + (enable ? "enabled" : "disabled"));
}

bool Logger::isEnabled(LogLevel level) const {
    return level >= minLevel.load();
}