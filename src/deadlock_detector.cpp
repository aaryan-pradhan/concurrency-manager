#include "../include/deadlock_detector.h"
#include "../include/transaction.h"
#include "../include/lock_manager.h"

DeadlockDetector::DeadlockDetector(LockManager &lock_manager, Logger &logger, uint64_t detection_interval_ms)
    : lock_manager_(lock_manager),
      logger_(logger),
      detection_interval_ms_(detection_interval_ms),
      running_(true)
{
    // Start the background thread for deadlock detection
    detection_thread_ = std::thread(&DeadlockDetector::DetectionThread, this);

    // Log initialization
    logger_.info("Deadlock Detector initialized with detection interval: " +
                 std::to_string(detection_interval_ms) + "ms");
}

DeadlockDetector::~DeadlockDetector()
{
    // Log shutdown
    logger_.info("Deadlock Detector shutting down");

    // Signal the detection thread to stop
    {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        running_ = false;
        cv_.notify_one();
    }

    // Wait for detection thread to finish
    if (detection_thread_.joinable())
    {
        detection_thread_.join();
    }

    logger_.info("Deadlock Detector thread terminated");
}

void DeadlockDetector::AddEdge(txn_id_t t1, txn_id_t t2)
{

    // Check if the edge already exists
    auto &edges = wait_for_graph_[t1];
    if (std::find(edges.begin(), edges.end(), t2) == edges.end())
    {
        // Add the edge if it doesn't exist
        edges.push_back(t2);

        logger_.debug("Added edge in wait-for graph: T" +
                      std::to_string(t1) + " -> T" + std::to_string(t2));
    }

    // Ensure t2 has an entry in the graph (even if it has no outgoing edges)
    if (wait_for_graph_.find(t2) == wait_for_graph_.end())
    {
        wait_for_graph_[t2] = std::vector<txn_id_t>();
    }
}

void DeadlockDetector::RemoveEdge(txn_id_t t1, txn_id_t t2)
{

    // Check if t1 exists in the graph
    auto it = wait_for_graph_.find(t1);
    if (it != wait_for_graph_.end())
    {
        // Check if the edge exists before logging
        bool edgeExists = std::find(it->second.begin(), it->second.end(), t2) != it->second.end();

        // Remove t2 from t1's edge list if it exists
        auto &edges = it->second;
        edges.erase(std::remove(edges.begin(), edges.end(), t2), edges.end());

        if (edgeExists)
        {
            logger_.debug("Removed edge from wait-for graph: T" +
                          std::to_string(t1) + " -> T" + std::to_string(t2));
        }
    }
}

bool DeadlockDetector::DFS(txn_id_t node, std::unordered_map<txn_id_t, bool> &visited,
                           std::unordered_map<txn_id_t, bool> &in_stack,
                           txn_id_t &youngest_txn)
{
    visited[node] = true;
    in_stack[node] = true;

    // Get neighbors
    auto it = wait_for_graph_.find(node);
    if (it != wait_for_graph_.end())
    {
        for (const auto &neighbor : it->second)
        {
            // If not visited, recursively explore
            if (!visited[neighbor])
            {
                if (DFS(neighbor, visited, in_stack, youngest_txn))
                {
                    // Update youngest transaction in the cycle if current is younger
                    youngest_txn = std::max(youngest_txn, node);
                    return true; // Cycle found
                }
            }
            // If already in recursion stack, we found a cycle
            else if (in_stack[neighbor])
            {
                youngest_txn = std::max(youngest_txn, node);

                logger_.debug("Cycle found in wait-for graph involving T" +
                              std::to_string(node) + " and T" +
                              std::to_string(neighbor));
                return true; // Cycle found
            }
        }
    }

    // Backtrack: remove node from recursion stack
    in_stack[node] = false;
    return false; // No cycle found in this path
}

bool DeadlockDetector::HasCycle(txn_id_t &txn_id)
{
    std::unordered_map<txn_id_t, bool> visited;
    std::unordered_map<txn_id_t, bool> in_stack;

    // Get all nodes and sort them (to explore in deterministic order)
    std::vector<txn_id_t> nodes;
    for (const auto &[node, _] : wait_for_graph_)
    {
        nodes.push_back(node);
    }

    std::sort(nodes.begin(), nodes.end());

    // Start DFS from each unvisited node
    for (const auto &node : nodes)
    {
        if (!visited[node])
        {
            // Initialize youngest transaction as current node
            txn_id = node;

            if (DFS(node, visited, in_stack, txn_id))
            {
                logger_.info("Deadlock cycle detected, youngest transaction: T" +
                             std::to_string(txn_id));
                return true; // Found a cycle
            }
        }
    }

    return false; // No cycles found
}

std::vector<std::pair<txn_id_t, txn_id_t>> DeadlockDetector::GetEdgeList()
{
    std::lock_guard<std::mutex> lock(graph_mutex_);

    std::vector<std::pair<txn_id_t, txn_id_t>> edge_list;

    // Iterate through the graph and add all edges to the list
    for (const auto &[from, to_list] : wait_for_graph_)
    {
        for (const auto &to : to_list)
        {
            edge_list.emplace_back(from, to);
        }
    }

    return edge_list;
}

void DeadlockDetector::BuildWaitForGraph()
{
    std::lock_guard<std::mutex> lock(graph_mutex_);

    // Clear any existing graph - build from scratch each time
    wait_for_graph_.clear();

    logger_.debug("Building wait-for graph");

    // For each resource in the system (assuming resource IDs are 0-10000)
    for (int resource_id = 0; resource_id < 10000; resource_id++)
    {
        // Get transactions waiting for this resource
        auto waiters = lock_manager_.getWaitingTransactions(resource_id);

        // Skip resources with no waiters
        if (waiters.empty())
            continue;

        // Get transactions currently holding locks on this resource
        auto holders = lock_manager_.getLockHolders(resource_id);

        if (!waiters.empty() && !holders.empty())
        {
            logger_.debug("Resource R" + std::to_string(resource_id) + " has " +
                          std::to_string(waiters.size()) + " waiters and " +
                          std::to_string(holders.size()) + " holders");

            // Log details about waiters and holders
            std::string waiter_ids;
            for (auto w : waiters)
                waiter_ids += "T" + std::to_string(w) + " ";

            std::string holder_ids;
            for (auto h : holders)
                holder_ids += "T" + std::to_string(h) + " ";

            logger_.debug("Resource R" + std::to_string(resource_id) +
                          " waiters: " + waiter_ids + ", holders: " + holder_ids);
        }

        // For each waiter, add edges to all holders (waiter -> holder)
        for (auto waiter_id : waiters)
        {
            // Skip aborted transactions
            Transaction *waiter = Transaction::GetTransaction(waiter_id);
            if (!waiter || waiter->getState() == TransactionState::ABORTED)
            {
                continue;
            }

            for (auto holder_id : holders)
            {
                // Skip aborted transactions or self-references
                if (holder_id == waiter_id)
                    continue;

                Transaction *holder = Transaction::GetTransaction(holder_id);
                if (!holder || holder->getState() == TransactionState::ABORTED)
                {
                    continue;
                }

                // Add edge from waiter to holder
                AddEdge(waiter_id, holder_id);
            }
        }
    }

    // Debug output - print the graph
    std::string graph_summary;
    for (const auto &[node, edges] : wait_for_graph_)
    {
        if (!edges.empty())
        {
            graph_summary += "T" + std::to_string(node) + " -> ";
            for (auto e : edges)
                graph_summary += "T" + std::to_string(e) + " ";
            graph_summary += "\n";
        }
    }

    if (!graph_summary.empty())
    {
        logger_.debug("Wait-for graph:\n" + graph_summary);
    }
}

void DeadlockDetector::RunCycleDetection()
{
    // Step 1: Build the wait-for graph
    BuildWaitForGraph();

    // Step 2: Detect and break all cycles
    int cyclesFound = 0;
    while (true)
    {
        txn_id_t youngest_txn;

        // If no cycle found, we're done
        if (!HasCycle(youngest_txn))
        {
            break;
        }

        cyclesFound++;

        // Get a pointer to the youngest transaction in the cycle
        Transaction *txn = Transaction::GetTransaction(youngest_txn);

        // If transaction exists, abort it to break the cycle
        if (txn != nullptr)
        {
            // Log before aborting
            logger_.warning("Aborting transaction T" + std::to_string(youngest_txn) +
                            " to break deadlock cycle");

            // Set the transaction's state to ABORTED
            txn->abort();

            // CRITICAL FIX: Release all locks held by the aborted transaction
            lock_manager_.releaseAllLocks(youngest_txn);

            // Remove the aborted transaction from the graph
            {
                std::lock_guard<std::mutex> lock(graph_mutex_);

                // Remove it as a source node
                wait_for_graph_.erase(youngest_txn);

                // Remove any edges to this transaction
                for (auto &[_, edges] : wait_for_graph_)
                {
                    edges.erase(std::remove(edges.begin(), edges.end(), youngest_txn), edges.end());
                }
            }
        }
        else
        {
            logger_.error("Failed to find transaction T" + std::to_string(youngest_txn) +
                          " to abort for deadlock resolution");
        }
    }

    if (cyclesFound > 0)
    {
        logger_.info("Deadlock detection resolved " + std::to_string(cyclesFound) +
                     " cycle(s)");
    }
}

void DeadlockDetector::DetectionThread()
{
    logger_.info("Deadlock detection thread started");

    while (running_)
    {
        // Run deadlock detection
        RunCycleDetection();

        // Check if we should stop
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(detection_interval_ms_),
                     [this]()
                     { return !running_; });
    }

    logger_.info("Deadlock detection thread stopping");
}