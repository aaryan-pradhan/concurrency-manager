#pragma once

#include <unordered_map>
#include <set>
#include <vector>
#include <mutex>
#include <string>
#include <sstream>
#include <fstream>  // Add this for file operations
#include "logger.h"

/**
 * @class ResourceAllocationGraph
 * @brief Maintains a resource allocation graph for deadlock detection
 */
class ResourceAllocationGraph {
private:
    // Map of resource IDs to transactions that hold locks on them (assignment edges)
    std::unordered_map<int, std::set<int>> assignmentEdges;
    
    // Map of transaction IDs to resources they are waiting for (request edges)
    std::unordered_map<int, std::set<int>> requestEdges;
    
    // Map of transaction IDs to resources they may request in the future (claim edges)
    std::unordered_map<int, std::set<int>> claimEdges;
    
    // Logger for recording operations
    Logger &logger;
    
    // File stream for RAG logging
    static std::ofstream ragLogFile;
    
    // Mutex for thread safety - changed to recursive mutex to allow nested locking
    mutable std::recursive_mutex mtx;

public:
    /**
     * @brief Constructs a new ResourceAllocationGraph
     * @param logger Reference to the logger for recording operations
     */
    explicit ResourceAllocationGraph(Logger &logger);
    
    /**
     * @brief Destructor to ensure log file is closed
     */
    ~ResourceAllocationGraph();
    
    /**
     * @brief Add an assignment edge (resource -> transaction)
     * @param resourceId ID of the resource
     * @param txnId ID of the transaction
     */
    void addAssignmentEdge(int resourceId, int txnId);
    
    /**
     * @brief Remove an assignment edge (resource -> transaction)
     * @param resourceId ID of the resource
     * @param txnId ID of the transaction
     */
    void removeAssignmentEdge(int resourceId, int txnId);
    
    /**
     * @brief Add a request edge (transaction -> resource)
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     */
    void addRequestEdge(int txnId, int resourceId);
    
    /**
     * @brief Remove a request edge (transaction -> resource)
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     */
    void removeRequestEdge(int txnId, int resourceId);
    
    /**
     * @brief Add a claim edge (transaction -> resource)
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     */
    void addClaimEdge(int txnId, int resourceId);
    
    /**
     * @brief Remove a claim edge (transaction -> resource)
     * @param txnId ID of the transaction
     * @param resourceId ID of the resource
     */
    void removeClaimEdge(int txnId, int resourceId);
    
    /**
     * @brief Check for a deadlock in the graph
     * @param deadlockCycle Output parameter that will contain the cycle if a deadlock is found
     * @return true if a deadlock is found, false otherwise
     */
    bool detectDeadlock(std::vector<int>& deadlockCycle);
    
    /**
     * @brief Clear all edges for a transaction (e.g., on commit/abort)
     * @param txnId ID of the transaction
     */
    void clearTransaction(int txnId);
    
    /**
     * @brief Convert the graph to a string representation
     * @return String representation of the graph
     */
    std::string toString() const;
    
    /**
     * @brief Log the current state of the RAG to the log file
     * @param transactionInfo Optional additional information about the current transaction
     */
    void logToFile(const std::string& transactionInfo = "") const;
    
    /**
     * @brief Initialize the RAG log file
     * @param filename The name of the log file
     * @return true if file opened successfully, false otherwise
     */
    static bool initLogFile(const std::string& filename = "RAGoutput.log");
    
private:
    /**
     * @brief Helper method for deadlock detection - checks for a cycle in the graph
     * @param startTxnId ID of the transaction to start the search from
     * @param path Current path in the search
     * @param visited Set of visited transaction IDs
     * @param recursionStack Set of transaction IDs in the current recursion stack
     * @return true if a cycle is found, false otherwise
     */
    bool hasCycle(int startTxnId, std::vector<int>& path, std::set<int>& visited, std::set<int>& recursionStack);
};