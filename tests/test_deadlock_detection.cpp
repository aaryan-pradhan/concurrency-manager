// Modified to remove all console output

#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <barrier>
#include <numeric> 
#include <iomanip> // For formatting in metrics file
#include "../include/concurrency_manager.h"

// Maps data items to resource IDs (shared by all transaction threads, guarded by resourceIdMutex)
std::map<std::string, int> dataItemToResourceId;
std::mutex resourceIdMutex;

int cnt = 1;

// Structure to track per-transaction metrics
struct TransactionMetrics {
    int txnNum;                                      // Logical transaction number
    int txnId;                                       // Actual transaction ID
    int priority;                                    // Transaction priority
    int restartCount;                                // Number of times restarted
    int lockAcquisitions;                            // Number of successful lock acquisitions
    int lockFailures;                                // Number of lock acquisition failures
    std::chrono::steady_clock::time_point startTime; // First start time
    std::chrono::steady_clock::time_point endTime;   // End time
    bool committed;                                  // Whether committed successfully
    bool abortedByDeadlock;                          // Whether aborted due to deadlock
    double latencyMs;                                // Total latency in milliseconds
    
    TransactionMetrics(int num = -1) : 
        txnNum(num), txnId(-1), priority(1), restartCount(0),
        lockAcquisitions(0), lockFailures(0), committed(false), 
        abortedByDeadlock(false), latencyMs(0.0) {}
};

// Global metrics tracking
std::mutex metricsMutex;
std::map<int, TransactionMetrics> txnMetrics;  // Map from txnNum to metrics
double maxLatency = 0.0;
std::atomic<int> completedTxnCount(0);
std::atomic<int> abortedTxnCount(0);
std::atomic<int> restartedTxnCount(0);

// Get or create resource ID for a data item
int getResourceId(const std::string &dataItem)
{
    std::lock_guard<std::mutex> lock(resourceIdMutex);
    if (dataItemToResourceId.find(dataItem) == dataItemToResourceId.end())
    {
        // Create a new resource ID
        dataItemToResourceId[dataItem] = cnt++;
    }
    return dataItemToResourceId[dataItem];
}

// Define a struct for transaction operations
struct Operation
{
    enum Type
    {
        START,
        READ,
        WRITE,
        COMMIT,
        ABORT,
        RELEASE
    };

    Type type;
    int txnNum;
    std::string item; // For read/write/release operations
    int lineNumber;

    std::string toString() const
    {
        std::string result;
        switch (type)
        {
        case START:
            result = "START T" + std::to_string(txnNum);
            break;
        case READ:
            result = "R" + std::to_string(txnNum) + "(" + item + ")";
            break;
        case WRITE:
            result = "W" + std::to_string(txnNum) + "(" + item + ")";
            break;
        case COMMIT:
            result = "C" + std::to_string(txnNum);
            break;
        case ABORT:
            result = "A" + std::to_string(txnNum);
            break;
        case RELEASE:
            result = "RELEASE T" + std::to_string(txnNum) + "(" + item + ")";
            break;
        }
        return result;
    }
};

// Output mutex to prevent output interleaving (still needed for file logs)
std::mutex outputMutex;
std::mutex stateMutex;

// Redefine the SYNCHRONIZED_COUT macro to do nothing
#define SYNCHRONIZED_COUT(x) {}

// Global atomic counter for tracking active threads
std::atomic<int> activeThreads(0);

// Flag to track if deadlock was detected
std::atomic<bool> deadlockDetected(false);

// Helper function to log RAG state to file only (no console output)
void logRAGState(ConcurrencyManager &cm, int txnNum, const std::string &operation)
{
    // Only log to file, skip console output
    cm.logResourceAllocationGraph("T" + std::to_string(txnNum) + " " + operation);
}

// Initialize transaction metrics
void initTransactionMetrics(int txnNum) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    if (txnMetrics.find(txnNum) == txnMetrics.end()) {
        txnMetrics[txnNum] = TransactionMetrics(txnNum);
    }
}

// Record transaction start
void recordTxnStart(int txnNum, int txnId, int priority) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    auto& metrics = txnMetrics[txnNum];
    
    // Record transaction ID and priority
    metrics.txnId = txnId;
    metrics.priority = priority;
    metrics.lockAcquisitions = 0;
    
    // If first start, record the start time
    if (metrics.startTime.time_since_epoch().count() == 0) {
        metrics.startTime = std::chrono::steady_clock::now();
    }
}

// Record lock acquisition
void recordLockAcquisition(int txnNum, bool success) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    if (success) {
        txnMetrics[txnNum].lockAcquisitions++;
    } else {
        txnMetrics[txnNum].lockFailures++;
    }
}

// Record transaction restart
void recordTxnRestart(int txnNum) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    txnMetrics[txnNum].restartCount++;
    txnMetrics[txnNum].abortedByDeadlock = true;
    restartedTxnCount++;
}

// Record transaction end
void recordTxnEnd(int txnNum, bool committed) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    auto& metrics = txnMetrics[txnNum];
    
    // Set end time and committed status
    metrics.endTime = std::chrono::steady_clock::now();
    metrics.committed = committed;
    
    // Calculate latency
    metrics.latencyMs = std::chrono::duration<double, std::milli>(
        metrics.endTime - metrics.startTime).count();
    
    // Update max latency
    maxLatency = std::max(maxLatency, metrics.latencyMs);
    
    // Increment completed count if committed
    if (committed) {
        completedTxnCount++;
    } else if (!metrics.abortedByDeadlock) {
        // Count explicit aborts (not from deadlock)
        abortedTxnCount++;
    }
}

void writeTransactionMetricsToFile(const std::string& testFile, std::chrono::steady_clock::time_point testStartTime) {
    std::ofstream metricsFile("transaction_metrics.txt", std::ios::app);
    if (!metricsFile.is_open()) {
        return; // Silently fail - no console output
    }
    
    auto testEndTime = std::chrono::steady_clock::now();
    double testDurationSec = std::chrono::duration<double>(testEndTime - testStartTime).count();
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    char timeBuffer[80];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
    
    // Calculate aggregate metrics
    double totalLatency = 0.0;
    int successfulTxns = 0;
    
    // Header with timestamp and test information
    metricsFile << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    metricsFile << "┃                     TRANSACTION METRICS REPORT                      ┃" << std::endl;
    metricsFile << "┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫" << std::endl;
    metricsFile << "┃ Test File: " << std::left << std::setw(58) << testFile << " ┃" << std::endl;
    metricsFile << "┃ Generated: " << std::left << std::setw(57) << timeBuffer << " ┃" << std::endl;
    metricsFile << "┃ Duration:  " << std::left << std::fixed << std::setprecision(2) 
                << std::setw(57) << testDurationSec << "s ┃" << std::endl;
    metricsFile << "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" << std::endl << std::endl;
    
    // Transaction details section
    metricsFile << "┏━━━━━━━━━━━━━━━━━━━━━━━━ TRANSACTION DETAILS ━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    metricsFile << "┣━━━━━━━━┳━━━━━━━━┳━━━━━━━━━━┳━━━━━━━━━━┳━━━━━━━━━━━━┳━━━━━━━━━━━━┳━━━━━━━━━━━━┳━━━━━━━━━━━┫" << std::endl;
    metricsFile << "┃ " << std::left 
                << std::setw(6) << "TxnNum" << " ┃ "
                << std::setw(6) << "TxnID" << " ┃ "
                << std::setw(8) << "Priority" << " ┃ "
                << std::setw(8) << "Restarts" << " ┃ "
                << std::setw(10) << "Locks Acq" << " ┃ "
                << std::setw(10) << "Locks Fail" << " ┃ "
                << std::setw(10) << "Latency(ms)" << " ┃ "
                << std::setw(9) << "Status" << " ┃" 
                << std::endl;
    metricsFile << "┣━━━━━━━━╋━━━━━━━━╋━━━━━━━━━━╋━━━━━━━━━━╋━━━━━━━━━━━━╋━━━━━━━━━━━━╋━━━━━━━━━━━━╋━━━━━━━━━━━┫" << std::endl;
    
    // Sort transactions by number for consistent output
    std::vector<int> txnNums;
    for (const auto& pair : txnMetrics) {
        txnNums.push_back(pair.first);
    }
    std::sort(txnNums.begin(), txnNums.end());
    
    for (int txnNum : txnNums) {
        const auto& metrics = txnMetrics[txnNum];
        
        // Status string
        std::string status;
        if (metrics.committed) {
            status = "Committed";
            totalLatency += metrics.latencyMs;
            successfulTxns++;
        } else if (metrics.abortedByDeadlock && metrics.restartCount > 0) {
            status = "Restarted";
        } else {
            status = "Aborted";
        }
        
        metricsFile << "┃ " << std::left 
                    << std::setw(6) << metrics.txnNum << " ┃ "
                    << std::setw(6) << metrics.txnId << " ┃ "
                    << std::setw(8) << metrics.priority << " ┃ "
                    << std::setw(8) << metrics.restartCount << " ┃ "
                    << std::setw(10) << metrics.lockAcquisitions << " ┃ "
                    << std::setw(10) << metrics.lockFailures << " ┃ "
                    << std::fixed << std::setprecision(2) << std::setw(10) << metrics.latencyMs << " ┃ "
                    << std::setw(9) << status << " ┃"
                    << std::endl;
    }
    
    metricsFile << "┗━━━━━━━━┻━━━━━━━━┻━━━━━━━━━━┻━━━━━━━━━━┻━━━━━━━━━━━━┻━━━━━━━━━━━━┻━━━━━━━━━━━━┻━━━━━━━━━━━┛" << std::endl;
    
    // Calculate aggregate metrics
    double avgLatency = successfulTxns > 0 ? totalLatency / successfulTxns : 0;
    double throughput = successfulTxns / testDurationSec;
    double successRate = (completedTxnCount.load() + restartedTxnCount.load() > 0) 
                        ? 100.0 * completedTxnCount.load() / (completedTxnCount.load() + restartedTxnCount.load() + abortedTxnCount.load()) 
                        : 0.0;
    
    // Summary section
    metricsFile << std::endl;
    metricsFile << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━ PERFORMANCE SUMMARY ━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    metricsFile << "┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫" << std::endl;
    metricsFile << "┃ Metric                                ┃ Value                              ┃" << std::endl;
    metricsFile << "┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫" << std::endl;
    metricsFile << "┃ Transactions Completed                ┃ " << std::left << std::setw(34) << completedTxnCount.load() << " ┃" << std::endl;
    metricsFile << "┃ Transactions Aborted                  ┃ " << std::left << std::setw(34) << abortedTxnCount.load() << " ┃" << std::endl;
    metricsFile << "┃ Transactions Restarted                ┃ " << std::left << std::setw(34) << restartedTxnCount.load() << " ┃" << std::endl;
    metricsFile << "┃ Transaction Success Rate              ┃ " << std::fixed << std::setprecision(1) << std::left << std::setw(33) << successRate << "% ┃" << std::endl;
    metricsFile << "┃ Throughput                            ┃ " << std::fixed << std::setprecision(2) << std::left << std::setw(32) << throughput << " txns/sec ┃" << std::endl;
    metricsFile << "┃ Average Transaction Latency           ┃ " << std::fixed << std::setprecision(2) << std::left << std::setw(32) << avgLatency << " ms ┃" << std::endl;
    metricsFile << "┃ Maximum Transaction Latency           ┃ " << std::fixed << std::setprecision(2) << std::left << std::setw(32) << maxLatency << " ms ┃" << std::endl;
    metricsFile << "┃ Deadlock Detected                     ┃ " << std::left << std::setw(34) << (deadlockDetected.load() ? "Yes" : "No") << " ┃" << std::endl;
    metricsFile << "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" << std::endl;
    metricsFile << std::endl;
    
    metricsFile.close();
}

ConcurrencyManager cm("deadlock_test.log", 100);
std::shared_ptr<std::barrier<>> startBarrier;

int calculateBackoff(int retryCount) {
    // Base backoff in milliseconds
    int baseBackoff = 20;
    
    // Maximum backoff to prevent extremely long waits
    int maxBackoff = 5000; // 5 seconds
    
    // Calculate exponential backoff with some randomization
    int backoff = std::min(baseBackoff * (1 << retryCount), maxBackoff);
    
    // Add jitter (±30%) to prevent thundering herd problem
    int jitter = (std::rand() % 60) - 30;
    backoff = backoff * (100 + jitter) / 100;
    
    // Ensure backoff is at least 5ms
    return std::max(backoff, 5);
}

// Run a transaction in its own thread
void runTransaction(int txnNum, const std::vector<Operation> &operations, int priority = 1)
{
    activeThreads++;

    startBarrier->arrive_and_wait();

    int txnId = -1;
    bool wasAborted = false;

    // Initialize metrics for this transaction
    initTransactionMetrics(txnNum);

    try
    {
        for (int i = 0; i < (int)operations.size(); i++)
        {
            const Operation &op = operations[i];
            // Handle START operation
            if (op.type == Operation::START)
            {
                txnId = cm.beginTransaction("Transaction " + std::to_string(txnNum), txnId, priority);
                recordTxnStart(txnNum, txnId, priority);
                
                // Log to file only
                logRAGState(cm, txnNum, "started");
                continue;
            }

            // Skip if transaction not started
            if (txnId == -1)
            {
                // Skip silently
                continue;
            }

            // Process operations only if not aborted
            switch (op.type)
            {
            case Operation::READ:
            {
                int resourceId = getResourceId(op.item);
                bool success = cm.acquireLock(txnId, resourceId, LockType::SHARED, true);
                recordLockAcquisition(txnNum, success);

                // Check if we were aborted while waiting
                if (cm.getTransactionState(txnId) == TransactionState::ABORTED)
                {
                    deadlockDetected = true;
                    wasAborted = true;
                    recordTxnRestart(txnNum);
                    i = -1; // Restart from the beginning
                    priority++;

                    int backoff = calculateBackoff(priority);

                    logRAGState(cm, txnNum, "was aborted while waiting for lock");

                    // Sleep for backoff time
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                    break;
                }

                if (success)
                {
                    logRAGState(cm, txnNum, "acquired READ lock on " + op.item);
                }
                break;
            }
            case Operation::WRITE:
            {
                int resourceId = getResourceId(op.item);
                bool success = cm.acquireLock(txnId, resourceId, LockType::EXCLUSIVE, true);
                recordLockAcquisition(txnNum, success);

                // Check if we were aborted while waiting
                if (cm.getTransactionState(txnId) == TransactionState::ABORTED)
                {
                    deadlockDetected = true;
                    wasAborted = true;
                    i = -1; // Restart from the beginning
                    priority++;

                    int backoff = calculateBackoff(priority);

                    recordTxnRestart(txnNum);
                    logRAGState(cm, txnNum, "was aborted while waiting for lock");

                    // Sleep for backoff time
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                    break;
                }

                if (success)
                {
                    logRAGState(cm, txnNum, "acquired WRITE lock on " + op.item);
                }
                break;
            }
            case Operation::COMMIT:
                if (cm.commitTransaction(txnId)) {
                    recordTxnEnd(txnNum, true);
                    wasAborted = false;
                } else if (cm.getTransactionState(txnId) == TransactionState::ABORTED) {
                    // Chosen as a deadlock victim after its last lock was granted: restart, don't exit uncommitted
                    deadlockDetected = true;
                    wasAborted = true;
                    recordTxnRestart(txnNum);
                    i = -1;
                    priority++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(calculateBackoff(priority)));
                    break;
                }
                logRAGState(cm, txnNum, "committing");
                break;
            case Operation::ABORT:
                cm.abortTransaction(txnId, "User requested abort");
                recordTxnEnd(txnNum, false);
                wasAborted = true;
                logRAGState(cm, txnNum, "explicitly aborting");
                break;
            case Operation::RELEASE:
            {
                int resourceId = getResourceId(op.item);
                cm.releaseLock(txnId, resourceId);
                logRAGState(cm, txnNum, "releasing lock on " + op.item);
                break;
            }
            }
        }
    }
    catch (const std::exception &e)
    {
        // Silent error handling - just record it in the log
        logRAGState(cm, txnNum, "error: " + std::string(e.what()));
    }

    std::cout << "Transaction T" << txnNum << " completed with ID " << txnId
              << (wasAborted ? " (aborted)" : " (committed)") << std::endl;
    std::cout << "Active threads: " << activeThreads.load() << std::endl;

    activeThreads--;
}

// Parse a test file into operations
std::vector<std::vector<Operation>> parseTestFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::map<int, std::vector<Operation>> txnOperations;

    std::string line;
    int lineNum = 0;

    // Regular expressions for parsing
    std::regex startRe("START T(\\d+)");
    std::regex readRe("R(\\d+)\\(([A-Za-z0-9]+)\\)");
    std::regex writeRe("W(\\d+)\\(([A-Za-z0-9]+)\\)");
    std::regex commitRe("C(\\d+)");
    std::regex abortRe("A(\\d+)");
    std::regex releaseRe("RELEASE T(\\d+)\\(([A-Za-z0-9]+)\\)");

    std::smatch match;

    while (std::getline(file, line))
    {
        lineNum++;

        // Skip empty lines and comments
        if (line.empty() || line.find("//") == 0)
        {
            continue;
        }

        // Remove inline comments
        auto commentPos = line.find("//");
        if (commentPos != std::string::npos)
        {
            line = line.substr(0, commentPos);
        }

        // Trim whitespace
        line = std::regex_replace(line, std::regex("^\\s+|\\s+$"), "");
        if (line.empty())
            continue;

        if (std::regex_search(line, match, startRe))
        {
            int txnNum = std::stoi(match[1]);
            txnOperations[txnNum].push_back({Operation::START, txnNum, "", lineNum});
        }
        else if (std::regex_search(line, match, readRe))
        {
            int txnNum = std::stoi(match[1]);
            // Remove debug print
            txnOperations[txnNum].push_back({Operation::READ, txnNum, match[2], lineNum});
        }
        else if (std::regex_search(line, match, writeRe))
        {
            int txnNum = std::stoi(match[1]);
            // Remove debug print
            txnOperations[txnNum].push_back({Operation::WRITE, txnNum, match[2], lineNum});
        }
        else if (std::regex_search(line, match, commitRe))
        {
            int txnNum = std::stoi(match[1]);
            txnOperations[txnNum].push_back({Operation::COMMIT, txnNum, "", lineNum});
        }
        else if (std::regex_search(line, match, abortRe))
        {
            int txnNum = std::stoi(match[1]);
            txnOperations[txnNum].push_back({Operation::ABORT, txnNum, "", lineNum});
        }
        else if (std::regex_search(line, match, releaseRe))
        {
            int txnNum = std::stoi(match[1]);
            txnOperations[txnNum].push_back({Operation::RELEASE, txnNum, match[2], lineNum});
        }
        // No warning for unparseable lines - silent operation
    }

    // Convert map to vector of operation vectors
    std::vector<std::vector<Operation>> result;
    for (const auto &pair : txnOperations)
    {
        result.push_back(pair.second);
    }

    return result;
}

int main(int argc, char *argv[])
{
    // Check command line arguments
    if (argc < 2)
    {
        // Output error to stderr (user still needs to know this)
        std::cerr << "Usage: " << argv[0] << " <test-file>" << std::endl;
        return 1;
    }

    std::string testFile = argv[1];
    auto testStartTime = std::chrono::steady_clock::now();

    try
    {
        // Parse test file
        auto transactionOperations = parseTestFile(testFile);

        int numTransactions = transactionOperations.size();

        // Initialize the barrier with the number of transactions
        startBarrier = std::make_shared<std::barrier<>>(numTransactions);
        
        // Create threads for each transaction
        std::vector<std::thread> threads;

        for (const auto &ops : transactionOperations)
        {
            if (ops.empty())
                continue;

            int txnNum = ops[0].txnNum;
            threads.emplace_back(runTransaction, txnNum, ops, 1); // Start with priority 1
        }

        // Wait for all threads to complete
        for (auto &t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }

        // Wait a bit to ensure deadlock detection has run
        std::this_thread::sleep_for(std::chrono::milliseconds(100 * 2));

        // Write transaction metrics to file
        writeTransactionMetricsToFile(testFile, testStartTime);
        
    }
    catch (const std::exception &e)
    {
        // Write error to file instead of console
        std::ofstream errorFile("test_error.txt");
        if (errorFile.is_open()) {
            errorFile << "Error: " << e.what() << std::endl;
            errorFile.close();
        }
        return 1;
    }

    return 0;
}