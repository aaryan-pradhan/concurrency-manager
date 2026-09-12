#include "../include/deadlock_detector.h"
#include "../include/transaction.h"
#include "../include/lock_manager.h"
#include <climits>

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
    std::lock_guard<std::mutex> lock(graph_mutex_);
    AddEdgeInternal(t1, t2);
}

void DeadlockDetector::AddEdgeInternal(txn_id_t t1, txn_id_t t2)
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
    std::lock_guard<std::mutex> lock(graph_mutex_);

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

bool DeadlockDetector::FindCycle(txn_id_t node, std::unordered_map<txn_id_t, bool> &visited,
                                 std::unordered_map<txn_id_t, bool> &in_stack,
                                 std::vector<txn_id_t> &path, std::vector<txn_id_t> &cycle)
{
    visited[node] = true;
    in_stack[node] = true;
    path.push_back(node);

    auto it = wait_for_graph_.find(node);
    if (it != wait_for_graph_.end())
    {
        for (const auto &neighbor : it->second)
        {
            if (!visited[neighbor])
            {
                if (FindCycle(neighbor, visited, in_stack, path, cycle))
                {
                    return true;
                }
            }
            else if (in_stack[neighbor])
            {
                // Back edge node -> neighbor: the cycle is the path segment from neighbor to node.
                // Nodes earlier on the path only wait on the cycle and are not part of it.
                auto start = std::find(path.begin(), path.end(), neighbor);
                cycle.assign(start, path.end());
                return true;
            }
        }
    }

    // Backtrack
    in_stack[node] = false;
    path.pop_back();
    return false;
}

bool DeadlockDetector::HasCycle(txn_id_t &txn_id)
{
    std::lock_guard<std::mutex> lock(graph_mutex_);
    return HasCycleInternal(txn_id);
}

bool DeadlockDetector::HasCycleInternal(txn_id_t &txn_id)
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

    for (const auto &node : nodes)
    {
        if (visited[node])
        {
            continue;
        }

        std::vector<txn_id_t> path;
        std::vector<txn_id_t> cycle;
        if (!FindCycle(node, visited, in_stack, path, cycle))
        {
            continue;
        }

        // Victim: lowest priority among the cycle's members; ties go to the highest id.
        // A transaction that no longer exists sorts first so it is simply dropped from the graph.
        txn_id_t victim = cycle.front();
        int victimPriority = INT_MAX;
        std::string members;
        for (txn_id_t member : cycle)
        {
            auto txn = Transaction::GetTransaction(member);
            int priority = txn ? txn->getPriority() : INT_MIN;
            members += "T" + std::to_string(member) + " ";
            if (priority < victimPriority || (priority == victimPriority && member > victim))
            {
                victim = member;
                victimPriority = priority;
            }
        }

        txn_id = victim;
        logger_.info("Deadlock cycle detected: " + members + "-> victim T" + std::to_string(victim) +
                     " (priority: " + std::to_string(victimPriority) + ")");
        return true;
    }

    return false;
}

int DeadlockDetector::RunCycleDetection()
{
    std::lock_guard<std::mutex> run(run_mutex_);
    std::lock_guard<std::mutex> lock(graph_mutex_);

    // Step 1: Build the wait-for graph
    BuildWaitForGraph();

    // Step 2: Detect and break all cycles
    int cyclesFound = 0;
    txn_id_t victimId = 0;
    while (HasCycleInternal(victimId))
    {
        cyclesFound++;

        auto txn = Transaction::GetTransaction(victimId);

        // abort() fails only if the victim committed after the snapshot; then its locks are
        // being released anyway. Either way the node is removed, so the loop always terminates.
        if (txn != nullptr && txn->abort())
        {
            logger_.warning("Aborting transaction T" + std::to_string(victimId) +
                            " (priority: " + std::to_string(txn->getPriority()) +
                            ") to break deadlock cycle");

            // Release all locks held by the aborted transaction and wake its waiting thread
            lock_manager_.releaseAllLocks(static_cast<int>(victimId));
        }
        else
        {
            logger_.info("Deadlock victim T" + std::to_string(victimId) +
                         " already finished; removing it from the wait-for graph");
        }

        // Remove the victim from the graph
        wait_for_graph_.erase(victimId);
        for (auto &[_, edges] : wait_for_graph_)
        {
            edges.erase(std::remove(edges.begin(), edges.end(), victimId), edges.end());
        }
    }

    if (cyclesFound > 0)
    {
        logger_.info("Deadlock detection resolved " + std::to_string(cyclesFound) +
                     " cycle(s)");
    }
    return cyclesFound;
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
    // Clear any existing graph - build from scratch each time
    wait_for_graph_.clear();

    // One consistent snapshot of (waiter, holder) pairs from the lock table,
    // instead of querying resource ids one at a time
    for (const auto &[waiter_id, holder_id] : lock_manager_.getWaitForEdges())
    {
        auto waiter = Transaction::GetTransaction(waiter_id);
        auto holder = Transaction::GetTransaction(holder_id);
        if (!waiter || waiter->getState() == TransactionState::ABORTED ||
            !holder || holder->getState() == TransactionState::ABORTED)
        {
            continue;
        }
        AddEdgeInternal(waiter_id, holder_id);
    }

    if (logger_.isEnabled(LogLevel::DEBUG))
    {
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
