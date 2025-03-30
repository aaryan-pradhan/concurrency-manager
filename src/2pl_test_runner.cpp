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
int getResourceId(const std::string& dataItem) {
    if (dataItemToResourceId.find(dataItem) == dataItemToResourceId.end()) {
        // Create a new resource ID starting from 1001
        dataItemToResourceId[dataItem] = 1001 + dataItemToResourceId.size();
    }
    return dataItemToResourceId[dataItem];
}

// Define a struct for transaction operations
struct Operation {
    enum Type { START, READ, WRITE, COMMIT, ABORT, RELEASE };
    
    Type type;
    int txnNum;
    std::string item;  // For read/write/release operations
    int lineNumber;
    
    std::string toString() const {
        std::string result;
        switch (type) {
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
#define SYNCHRONIZED_COUT(x) { std::lock_guard<std::mutex> lock(outputMutex); std::cout << x << std::endl; }

// Global atomic counter for tracking active threads
std::atomic<int> activeThreads(0);

// Run a transaction in its own thread
void runTransaction(int txnNum, const std::vector<Operation>& operations, ConcurrencyManager& cm, 
                   std::map<int, int>& txnIdMap) {
    activeThreads++;
    
    int txnId = -1;
    
    try {
        for (const auto& op : operations) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + rand() % 50)); // Add some randomness
            
            // Handle START operation
            if (op.type == Operation::START) {
                txnId = cm.beginTransaction("Transaction " + std::to_string(txnNum));
                txnIdMap[txnNum] = txnId;
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": Started T" 
                              << txnNum << " (ID: " << txnId << ") [Line " << op.lineNumber << "]");
                continue;
            }
            
            if (txnId == -1) {
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": Error - T" 
                              << txnNum << " not started before operation at line " << op.lineNumber);
                continue;
            }
            
            // Handle READ operation
            if (op.type == Operation::READ) {
                int resourceId = getResourceId(op.item);
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " attempting to read " << op.item << " [Line " << op.lineNumber << "]");
                
                bool success = cm.acquireLock(txnId, resourceId, LockType::SHARED);
                
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " read " << op.item << ": " << (success ? "SUCCESS" : "FAILED")
                              << " [Line " << op.lineNumber << "]");
            }
            
            // Handle WRITE operation
            else if (op.type == Operation::WRITE) {
                int resourceId = getResourceId(op.item);
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " attempting to write " << op.item << " [Line " << op.lineNumber << "]");
                
                std::set<int> heldLocks = cm.getLocksHeldBy(txnId);
                bool hasLock = heldLocks.find(resourceId) != heldLocks.end();
                
                bool success = cm.acquireLock(txnId, resourceId, LockType::EXCLUSIVE);
                
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " write " << op.item << (hasLock ? " [upgrade]" : "") 
                              << ": " << (success ? "SUCCESS" : "FAILED")
                              << " [Line " << op.lineNumber << "]");
            }
            
            // Handle COMMIT operation
            else if (op.type == Operation::COMMIT) {
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " committing [Line " << op.lineNumber << "]");
                
                bool success = cm.commitTransaction(txnId);
                
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " commit: " << (success ? "SUCCESS" : "FAILED")
                              << " [Line " << op.lineNumber << "]");
            }
            
            // Handle ABORT operation
            else if (op.type == Operation::ABORT) {
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " aborting [Line " << op.lineNumber << "]");
                
                bool success = cm.abortTransaction(txnId);
                
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " abort: " << (success ? "SUCCESS" : "FAILED")
                              << " [Line " << op.lineNumber << "]");
            }
            
            // Handle RELEASE operation
            else if (op.type == Operation::RELEASE) {
                int resourceId = getResourceId(op.item);
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " releasing lock on " << op.item << " [Line " << op.lineNumber << "]");
                
                bool success = cm.releaseLock(txnId, resourceId);
                TransactionState state = cm.getTransactionState(txnId);
                std::string stateStr = state == TransactionState::GROWING ? "GROWING" : 
                                      (state == TransactionState::SHRINKING ? "SHRINKING" :
                                      (state == TransactionState::COMMITTED ? "COMMITTED" : "ABORTED"));
                
                SYNCHRONIZED_COUT("Thread " << std::this_thread::get_id() << ": T" << txnNum 
                              << " released lock on " << op.item << ": " 
                              << (success ? "SUCCESS" : "FAILED")
                              << " - Now in " << stateStr << " phase"
                              << " [Line " << op.lineNumber << "]");
            }
        }
    } catch (const std::exception& e) {
        SYNCHRONIZED_COUT("Exception in transaction T" << txnNum << ": " << e.what());
        
        // Try to abort in case of exception
        if (txnId != -1) {
            cm.abortTransaction(txnId, "Exception: " + std::string(e.what()));
        }
    }
    
    activeThreads--;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <test_file.txt>" << std::endl;
        return 1;
    }
    
    std::string testFilePath = argv[1];
    std::ifstream testFile(testFilePath);
    
    if (!testFile) {
        std::cerr << "Error: Could not open test file: " << testFilePath << std::endl;
        return 1;
    }
    
    // Initialize concurrency manager
    ConcurrencyManager cm("2pl_test_results.log");
    
    // Parse test file first
    std::map<int, std::vector<Operation>> transactionOperations;
    std::string line;
    int lineNumber = 0;
    
    // Define regex patterns
    std::regex startRegex("START\\s+T(\\d+)");
    std::regex readRegex("R(\\d+)\\s*\\(([A-Za-z0-9]+)\\)");
    std::regex writeRegex("W(\\d+)\\s*\\(([A-Za-z0-9]+)\\)");
    std::regex commitRegex("C(\\d+)");
    std::regex abortRegex("A(\\d+)");
    std::regex releaseRegex("RELEASE\\s+T(\\d+)\\s*\\(([A-Za-z0-9]+)\\)");
    std::smatch match;
    
    std::cout << "Parsing 2PL test file: " << testFilePath << std::endl;
    
    // First pass: parse the file and group operations by transaction
    while (std::getline(testFile, line)) {
        lineNumber++;
        
        // Skip comments and empty lines
        if (line.empty() || line.substr(0, 2) == "//" || 
            line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        Operation op;
        op.lineNumber = lineNumber;
        
        // Match START
        if (std::regex_search(line, match, startRegex)) {
            op.type = Operation::START;
            op.txnNum = std::stoi(match[1]);
            transactionOperations[op.txnNum].push_back(op);
        }
        // Match READ
        else if (std::regex_search(line, match, readRegex)) {
            op.type = Operation::READ;
            op.txnNum = std::stoi(match[1]);
            op.item = match[2];
            transactionOperations[op.txnNum].push_back(op);
        }
        // Match WRITE
        else if (std::regex_search(line, match, writeRegex)) {
            op.type = Operation::WRITE;
            op.txnNum = std::stoi(match[1]);
            op.item = match[2];
            transactionOperations[op.txnNum].push_back(op);
        }
        // Match COMMIT
        else if (std::regex_search(line, match, commitRegex)) {
            op.type = Operation::COMMIT;
            op.txnNum = std::stoi(match[1]);
            transactionOperations[op.txnNum].push_back(op);
        }
        // Match ABORT
        else if (std::regex_search(line, match, abortRegex)) {
            op.type = Operation::ABORT;
            op.txnNum = std::stoi(match[1]);
            transactionOperations[op.txnNum].push_back(op);
        }
        // Match RELEASE
        else if (std::regex_search(line, match, releaseRegex)) {
            op.type = Operation::RELEASE;
            op.txnNum = std::stoi(match[1]);
            op.item = match[2];
            transactionOperations[op.txnNum].push_back(op);
        }
        else {
            std::cout << "Line " << lineNumber << ": Unrecognized command: " << line << std::endl;
        }
    }
    
    // Print the transaction operation plan
    std::cout << "\nTransaction operation plan:" << std::endl;
    for (const auto& [txnNum, ops] : transactionOperations) {
        std::cout << "Transaction T" << txnNum << ":" << std::endl;
        for (const auto& op : ops) {
            std::cout << "  - Line " << op.lineNumber << ": " << op.toString() << std::endl;
        }
    }
    
    // Launch threads for each transaction
    std::cout << "\nStarting transaction threads..." << std::endl;
    std::map<int, int> txnIdMap;
    std::vector<std::thread> threads;
    
    // Seed random number generator for sleep times
    srand(static_cast<unsigned int>(time(nullptr)));
    
    for (const auto& [txnNum, ops] : transactionOperations) {
        threads.emplace_back(runTransaction, txnNum, std::ref(ops), std::ref(cm), std::ref(txnIdMap));
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Stagger thread start
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // Print final system state
    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << "Test execution completed. Final system state:\n" << std::endl;
    std::cout << cm.getSystemState() << std::endl;
    
    std::cout << "All resources cleaned up. Exiting..." << std::endl;
    
    return 0;
}