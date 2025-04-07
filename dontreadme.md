Sure! Here's a clean and structured **Markdown** version of the explanation you provided:

---

# Edge Types in Resource Allocation Graph (RAG)

The Resource Allocation Graph (RAG) is a tool used to model the interactions between transactions and resources, especially for detecting and preventing deadlocks. Based on the code analysis, here’s when each type of edge is added:

---

## 1. **Assignment Edge** (Resource → Transaction)

An **assignment edge** is added when a transaction **successfully acquires a lock** on a resource.

### ✅ Scenarios:

- **When a lock is granted immediately:**

```cpp
if (canGrantImmediately) {
    newRequest.granted = true;
    lockTable[resourceId].push_back(newRequest);
    
    // Add assignment edge
    rag.addAssignmentEdge(resourceId, txnId);
}
```

- **When a waiting transaction finally gets the lock:**

```cpp
if (it != requests.end() && isCompatible(resourceId, *it)) {
    it->granted = true;
    
    // Convert request edge to assignment edge
    rag.addAssignmentEdge(resourceId, txnId);
    rag.removeRequestEdge(txnId, resourceId);
}
```

---

## 2. **Request Edge** (Transaction → Resource)

A **request edge** is added when a transaction **wants to acquire a lock but must wait** due to incompatible locking.

### 🔄 Scenario:

- **When a lock cannot be granted immediately and the transaction will wait:**

```cpp
if (!canGrantImmediately && wait) {
    // Add request edge to RAG
    rag.addRequestEdge(txnId, resourceId);
    
    // Add the request to the queue
    lockTable[resourceId].push_back(newRequest);
}
```

---

## 3. **Claim Edge** (Transaction → Resource)

**Claim edges** represent **potential future resource requests** by a transaction.

### ⚠️ Notes:

- **Not actively used** in the current lock operations.
- Used in **conservative deadlock prevention** or **avoidance algorithms** like the *Banker's Algorithm*.
- Useful for analyzing possible future conflicts.

### 👇 Methods Exist:

```cpp
void ResourceAllocationGraph::addClaimEdge(int txnId, int resourceId);
void ResourceAllocationGraph::removeClaimEdge(int txnId, int resourceId);
```

> These edges would typically be added **at the start of a transaction** if implementing conservative locking protocols.

---

Let me know if you'd like this turned into a table or visual flow!