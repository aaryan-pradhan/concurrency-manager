#include "../include/resource_manager.h"

ResourceAllocationGraph::ResourceAllocationGraph(Logger &loggerRef)
    : logger(loggerRef) {
    logger.info("Resource Allocation Graph initialized");
    
    // Initialize log file
    if (!initLogFile()) {
        logger.error("Failed to initialize log file for Resource Allocation Graph");
        throw std::runtime_error("Failed to initialize log file for Resource Allocation Graph");
    }

    std::cout << "Resource Allocation Graph initialized" << std::endl;
}

// Initialize the log file
bool ResourceAllocationGraph::initLogFile(const std::string& filename) {

    std::ofstream ragLogFile(filename, std::ios::app);

    if (!ragLogFile.is_open()) {
        return false;
    }
    
    // Write header
    ragLogFile << "=== RESOURCE ALLOCATION GRAPH LOG ===" << std::endl;
    ragLogFile << "Started at: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
    ragLogFile << "=======================================\n" << std::endl;
    
    return true;
}

// Destructor to close the file
ResourceAllocationGraph::~ResourceAllocationGraph() {
    std::ofstream ragLogFile("rag_log.txt", std::ios::app);
}

void ResourceAllocationGraph::addAssignmentEdge(int resourceId, int txnId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    assignmentEdges[resourceId].insert(txnId);
    // Remove request edge if it exists
    removeRequestEdge(txnId, resourceId);
    logger.debug("Added assignment edge: R" + std::to_string(resourceId) + " → T" + std::to_string(txnId));
}

void ResourceAllocationGraph::removeAssignmentEdge(int resourceId, int txnId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    if (assignmentEdges.find(resourceId) != assignmentEdges.end()) {
        assignmentEdges[resourceId].erase(txnId);
        if (assignmentEdges[resourceId].empty()) {
            assignmentEdges.erase(resourceId);
        }
        logger.debug("Removed assignment edge: R" + std::to_string(resourceId) + " → T" + std::to_string(txnId));
    }
}

void ResourceAllocationGraph::addRequestEdge(int txnId, int resourceId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    requestEdges[txnId].insert(resourceId);
    logger.debug("Added request edge: T" + std::to_string(txnId) + " → R" + std::to_string(resourceId));
}

void ResourceAllocationGraph::removeRequestEdge(int txnId, int resourceId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    if (requestEdges.find(txnId) != requestEdges.end()) {
        requestEdges[txnId].erase(resourceId);
        if (requestEdges[txnId].empty()) {
            requestEdges.erase(txnId);
        }
        logger.debug("Removed request edge: T" + std::to_string(txnId) + " → R" + std::to_string(resourceId));
    }
}

void ResourceAllocationGraph::addClaimEdge(int txnId, int resourceId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    claimEdges[txnId].insert(resourceId);
    logger.debug("Added claim edge: T" + std::to_string(txnId) + " → R" + std::to_string(resourceId));
}

void ResourceAllocationGraph::removeClaimEdge(int txnId, int resourceId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    if (claimEdges.find(txnId) != claimEdges.end()) {
        claimEdges[txnId].erase(resourceId);
        if (claimEdges[txnId].empty()) {
            claimEdges.erase(txnId);
        }
        logger.debug("Removed claim edge: T" + std::to_string(txnId) + " → R" + std::to_string(resourceId));
    }
}

bool ResourceAllocationGraph::detectDeadlock(std::vector<int>& deadlockCycle) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    
    // Start DFS from each transaction that is waiting for a resource
    for (const auto& entry : requestEdges) {
        int startTxnId = entry.first;
        std::set<int> visited;
        std::set<int> recursionStack;
        std::vector<int> path;
        
        if (hasCycle(startTxnId, path, visited, recursionStack)) {
            deadlockCycle = path;
            return true;
        }
    }
    
    return false;
}

bool ResourceAllocationGraph::hasCycle(int txnId, std::vector<int>& path, std::set<int>& visited, std::set<int>& recursionStack) {
    // Mark the current transaction as visited and part of recursion stack
    visited.insert(txnId);
    recursionStack.insert(txnId);
    path.push_back(txnId);
    
    // Check all resources this transaction is waiting for
    if (requestEdges.find(txnId) != requestEdges.end()) {
        for (int resourceId : requestEdges[txnId]) {
            // Track the resource in the path for visualization
            path.push_back(-resourceId);  // Use negative to distinguish from transaction IDs
            
            // Check all transactions that hold locks on this resource
            if (assignmentEdges.find(resourceId) != assignmentEdges.end()) {
                for (int holderTxnId : assignmentEdges[resourceId]) {
                    // If the holder is in the recursion stack, there's a cycle
                    if (recursionStack.find(holderTxnId) != recursionStack.end()) {
                        // Add the holder to complete the cycle
                        path.push_back(holderTxnId);
                        return true;
                    }
                    
                    // If the holder has not been visited, recurse on it
                    if (visited.find(holderTxnId) == visited.end()) {
                        if (hasCycle(holderTxnId, path, visited, recursionStack)) {
                            return true;
                        }
                    }
                }
            }
            
            // Remove the resource if no cycle found through this path
            path.pop_back();
        }
    }
    
    // Remove the transaction from recursion stack and path
    recursionStack.erase(txnId);
    path.pop_back();
    return false;
}

void ResourceAllocationGraph::clearTransaction(int txnId) {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    
    // Clear request edges
    requestEdges.erase(txnId);
    
    // Clear claim edges
    claimEdges.erase(txnId);
    
    // Clear assignment edges
    for (auto it = assignmentEdges.begin(); it != assignmentEdges.end(); ) {
        it->second.erase(txnId);
        if (it->second.empty()) {
            it = assignmentEdges.erase(it);
        } else {
            ++it;
        }
    }
    
    logger.debug("Cleared all edges for Transaction T" + std::to_string(txnId));
}

std::string ResourceAllocationGraph::toString() const {
    std::lock_guard<std::recursive_mutex> lock(mtx);  // Changed to recursive_mutex
    std::stringstream ss;
    ss << "\n=== RESOURCE ALLOCATION GRAPH ===\n";
    
    // Print assignment edges
    ss << "Assignment Edges (R → T):\n";
    for (const auto& entry : assignmentEdges) {
        int resourceId = entry.first;
        for (int txnId : entry.second) {
            ss << "  R" << resourceId << " → T" << txnId << "\n";
        }
    }
    
    // Print request edges
    ss << "Request Edges (T → R):\n";
    for (const auto& entry : requestEdges) {
        int txnId = entry.first;
        for (int resourceId : entry.second) {
            ss << "  T" << txnId << " → R" << resourceId << "\n";
        }
    }
    
    // Print claim edges
    ss << "Claim Edges (T → R):\n";
    for (const auto& entry : claimEdges) {
        int txnId = entry.first;
        for (int resourceId : entry.second) {
            ss << "  T" << txnId << " → R" << resourceId << " (claim)\n";
        }
    }
    
    ss << "===============================\n";
    return ss.str();
}

// Log to file method
void ResourceAllocationGraph::logToFile(const std::string& transactionInfo) const {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    
    std::ofstream ragLogFile = std::ofstream("rag_log.txt", std::ios::app);

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    // Write timestamp and optional transaction info
    ragLogFile << "\n[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] ";
    if (!transactionInfo.empty()) {
        ragLogFile << transactionInfo << std::endl;
    } else {
        ragLogFile << "RAG update" << std::endl;
    }
    
    // Write the actual RAG
    ragLogFile << toString();
    ragLogFile.flush();
}