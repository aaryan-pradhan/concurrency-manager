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
#include "../include/concurrency_manager.h"

// Maps data items to resource IDs
std::map<std::string, int> dataItemToResourceId;

// Get or create resource ID for a data item
int getResourceId(const std::string &dataItem)
{
    if (dataItemToResourceId.find(dataItem) == dataItemToResourceId.end())
    {
        // Create a new resource ID starting from 1001
        dataItemToResourceId[dataItem] = 1001 + dataItemToResourceId.size();
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

// Output mutex to prevent output interleaving
std::mutex outputMutex;
std::mutex stateMutex;
#define SYNCHRONIZED_COUT(x)                           \
    {                                                  \
        std::lock_guard<std::mutex> lock(outputMutex); \
        std::cout << x << std::endl;                   \
    }

// Global atomic counter for tracking active threads
std::atomic<int> activeThreads(0);

// Flag to track if deadlock was detected
std::atomic<bool> deadlockDetected(false);

// Add this helper function to log RAG state
void logRAGState(ConcurrencyManager &cm, int txnNum, const std::string &operation) {
    SYNCHRONIZED_COUT("\n--- RAG STATE AFTER T" << txnNum << " " << operation << " ---");
    SYNCHRONIZED_COUT(cm.getResourceAllocationGraph());
    SYNCHRONIZED_COUT("--------------------------------------\n");
    
    // Also log to file for persistent records
    cm.logResourceAllocationGraph("T" + std::to_string(txnNum) + " " + operation);
}

ConcurrencyManager cm("deadlock_test.log", 100);

// Run a transaction in its own thread
void runTransaction(int txnNum, const std::vector<Operation> &operations)
{
    activeThreads++;

    int txnId = -1;
    bool wasAborted = false;

    try
    {
        for (const auto &op : operations)
        {
            // Add a small delay between operations for realism
            // std::this_thread::sleep_for(std::chrono::milliseconds(10 + rand() % 50));

            // Handle START operation
            if (op.type == Operation::START)
            {
                txnId = cm.beginTransaction("Transaction " + std::to_string(txnNum));
                SYNCHRONIZED_COUT("Started T" << txnNum << " (ID: " << txnId << ")");
                logRAGState(cm, txnNum, "started");
                continue;
            }

            // Skip if transaction not started
            if (txnId == -1)
            {
                SYNCHRONIZED_COUT("T" << txnNum << " skipping operation " << op.toString()
                                      << " because transaction was not started");
                continue;
            }

            // Check if transaction was aborted (by deadlock detector or otherwise)
            if (cm.getTransactionState(txnId) == TransactionState::ABORTED)
            {
                if (!wasAborted)
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " was aborted by deadlock detector, stopping execution!");
                    deadlockDetected = true;
                    wasAborted = true;
                    logRAGState(cm, txnNum, "was aborted by deadlock detector");
                }
                break;
            }

            // Process operations only if not aborted
            switch (op.type)
            {
            case Operation::READ:
            {
                int resourceId = getResourceId(op.item);
                SYNCHRONIZED_COUT("T" << txnNum << " requesting READ lock on " << op.item
                                      << " (Resource " << resourceId << ")");
                bool success = cm.acquireLock(txnId, resourceId, LockType::SHARED, true);

                // Check if we were aborted while waiting
                if (cm.getTransactionState(txnId) == TransactionState::ABORTED)
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " was aborted while waiting for lock!");
                    deadlockDetected = true;
                    wasAborted = true;
                    logRAGState(cm, txnNum, "was aborted while waiting for lock");
                    break;
                }

                if (success)
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " acquired READ lock on " << op.item);
                    logRAGState(cm, txnNum, "acquired READ lock on " + op.item);
                }
                else
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " failed to acquire READ lock on " << op.item);
                }
                break;
            }
            case Operation::WRITE:
            {
                int resourceId = getResourceId(op.item);
                SYNCHRONIZED_COUT("T" << txnNum << " requesting WRITE lock on " << op.item
                                      << " (Resource " << resourceId << ")");
                bool success = cm.acquireLock(txnId, resourceId, LockType::EXCLUSIVE, true);

                // Check if we were aborted while waiting
                if (cm.getTransactionState(txnId) == TransactionState::ABORTED)
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " was aborted while waiting for lock!");
                    deadlockDetected = true;
                    wasAborted = true;
                    logRAGState(cm, txnNum, "was aborted while waiting for lock");
                    break;
                }

                if (success)
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " acquired WRITE lock on " << op.item);
                    logRAGState(cm, txnNum, "acquired WRITE lock on " + op.item);
                }
                else
                {
                    SYNCHRONIZED_COUT("T" << txnNum << " failed to acquire WRITE lock on " << op.item);
                }
                break;
            }
            case Operation::COMMIT:
                SYNCHRONIZED_COUT("T" << txnNum << " committing");
                cm.commitTransaction(txnId);
                logRAGState(cm, txnNum, "committing");
                break;
            case Operation::ABORT:
                SYNCHRONIZED_COUT("T" << txnNum << " explicitly aborting");
                cm.abortTransaction(txnId, "User requested abort");
                wasAborted = true;
                logRAGState(cm, txnNum, "explicitly aborting");
                break;
            case Operation::RELEASE:
            {
                int resourceId = getResourceId(op.item);
                SYNCHRONIZED_COUT("T" << txnNum << " releasing lock on " << op.item);
                cm.releaseLock(txnId, resourceId);
                logRAGState(cm, txnNum, "releasing lock on " + op.item);
                break;
            }
            }
        }
    }
    catch (const std::exception &e)
    {
        SYNCHRONIZED_COUT("Error in T" << txnNum << ": " << e.what());
    }

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
            txnOperations[txnNum].push_back({Operation::READ, txnNum, match[2], lineNum});
        }
        else if (std::regex_search(line, match, writeRe))
        {
            int txnNum = std::stoi(match[1]);
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
        else
        {
            SYNCHRONIZED_COUT("Warning: Could not parse line " << lineNum << ": " << line);
        }
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
        std::cerr << "Usage: " << argv[0] << " <test-file>" << std::endl;
        return 1;
    }

    std::string testFile = argv[1];

    try
    {
        // Parse test file
        auto transactionOperations = parseTestFile(testFile);

        // Create threads for each transaction
        std::vector<std::thread> threads;
        
        for (const auto &ops : transactionOperations)
        {
            if (ops.empty())
                continue;

            int txnNum = ops[0].txnNum;
            threads.emplace_back(runTransaction, txnNum, ops);
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

        // Display final test results and system state
        SYNCHRONIZED_COUT("Test completed. Final system state:");
        SYNCHRONIZED_COUT(cm.getSystemState());

        if (deadlockDetected)
        {
            SYNCHRONIZED_COUT("✅ PASSED: Deadlock was detected and resolved!");
        }
        else
        {
            SYNCHRONIZED_COUT("❌ FAILED: No deadlock was detected!");
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}