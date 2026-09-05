# 21. Glossary

Every term used across this documentation set, in one place, for quick lookup.

**2PL (Two-Phase Locking)** — A locking discipline where every transaction has a growing phase (acquire only) followed by a shrinking phase (release only), guaranteeing serializability. See [05](05-two-phase-locking-protocol.md).

**ACID** — Atomicity, Consistency, Isolation, Durability: the four properties transactions are expected to guarantee. See [02](02-databases-and-transactions.md).

**Assignment edge** — In a Resource Allocation Graph, an edge from a resource to a transaction meaning "this transaction currently holds this resource." See [07](07-resource-allocation-graphs.md).

**Atomic (atomicity, transaction-level)** — All of a transaction's operations happen, or none do. See [02](02-databases-and-transactions.md).

**Atomic (concurrency, `std::atomic`)** — A C++ type whose operations (like increment) are guaranteed uninterruptible without needing a mutex. See [03](03-concurrency-threads-and-race-conditions.md).

**Backoff (exponential backoff)** — A retry strategy where the wait time between attempts roughly doubles each time, to reduce repeated collisions. See [08](08-wait-for-graphs-and-victim-selection.md).

**Circular wait** — One of the four Coffman conditions for deadlock: a cycle of transactions each waiting on the next. See [06](06-deadlocks-explained.md).

**Claim edge** — In a Resource Allocation Graph, an edge representing a resource a transaction might request in the future (used in deadlock avoidance; not actively used in this codebase). See [07](07-resource-allocation-graphs.md).

**Coffman conditions** — The four conditions (mutual exclusion, hold-and-wait, no preemption, circular wait) that must all hold for a deadlock to be possible. See [06](06-deadlocks-explained.md).

**Commit** — A transaction's successful, permanent completion. See [02](02-databases-and-transactions.md).

**Compatibility (lock compatibility)** — Whether two lock requests on the same resource can both be granted at once. See [04](04-locks-and-concurrency-control.md).

**Concurrency control** — The set of mechanisms a database uses to let transactions run at the same time without corrupting data. See [02](02-databases-and-transactions.md).

**Condition variable** — A synchronization primitive that lets a thread sleep until explicitly woken by another thread. See [03](03-concurrency-threads-and-race-conditions.md).

**Deadlock** — A state where transactions are stuck forever, each waiting for a resource held by another in a cycle. See [06](06-deadlocks-explained.md).

**Deadlock avoidance** — A strategy that checks, before granting a lock, whether doing so could eventually lead to deadlock. See [06](06-deadlocks-explained.md).

**Deadlock detection** — A strategy that lets deadlocks happen and periodically scans for and resolves them. This project's actual strategy. See [06](06-deadlocks-explained.md), [08](08-wait-for-graphs-and-victim-selection.md).

**Deadlock prevention** — A strategy that structurally makes deadlocks impossible by design. See [06](06-deadlocks-explained.md).

**DFS (Depth-First Search)** — A graph traversal algorithm used here to find cycles in both the Resource Allocation Graph and the wait-for graph. See [07](07-resource-allocation-graphs.md), [08](08-wait-for-graphs-and-victim-selection.md).

**Exclusive lock** — A lock type allowing only one transaction to hold it, blocking all other locks on that resource; used for writes. See [04](04-locks-and-concurrency-control.md).

**Growing phase** — The 2PL phase during which a transaction may only acquire locks. See [05](05-two-phase-locking-protocol.md).

**Hold and wait** — One of the four Coffman conditions: a transaction can hold one resource while requesting another. See [06](06-deadlocks-explained.md).

**Isolation** — The ACID property that concurrent transactions don't interfere with each other. See [02](02-databases-and-transactions.md).

**Lock** — A permission record a transaction must hold before touching a resource. See [04](04-locks-and-concurrency-control.md).

**Lock table** — The `LockManager`'s central map from resource ID to the list of lock requests (granted and waiting) on it. See [11](11-module-lock-manager.md).

**Lock upgrade** — Converting an already-held SHARED lock into an EXCLUSIVE lock. See [04](04-locks-and-concurrency-control.md), [11](11-module-lock-manager.md).

**Lost update** — A concurrency bug where one transaction's write is silently overwritten/ignored because of unsynchronized interleaving. See [02](02-databases-and-transactions.md).

**Mutex** — A low-level lock ensuring only one thread accesses a piece of data at a time. See [03](03-concurrency-threads-and-race-conditions.md).

**Mutual exclusion** — One of the four Coffman conditions: a resource can only be held by one transaction at a time. See [06](06-deadlocks-explained.md).

**Priority (transaction priority)** — An integer assigned to a transaction, used to pick the least important (lowest-priority) transaction as a victim during deadlock resolution. See [08](08-wait-for-graphs-and-victim-selection.md).

**Race condition** — A bug arising from unsynchronized concurrent access to shared memory. See [03](03-concurrency-threads-and-race-conditions.md).

**RAG (Resource Allocation Graph)** — A graph of transactions and resources whose cycles indicate deadlocks. See [07](07-resource-allocation-graphs.md).

**Request edge** — In a Resource Allocation Graph, an edge from a transaction to a resource meaning "this transaction wants this resource but doesn't have it yet." See [07](07-resource-allocation-graphs.md).

**Resource** — In this project, an abstract, uniquely-numbered stand-in for a piece of data (there's no actual stored value). See [01](01-introduction.md).

**Serializability** — The property that a concurrent execution of transactions produces the same result as *some* one-at-a-time ordering of them, even though they actually overlapped in time. See [02](02-databases-and-transactions.md).

**Shared lock** — A lock type that many transactions can hold simultaneously on the same resource; used for reads. See [04](04-locks-and-concurrency-control.md).

**Shrinking phase** — The 2PL phase during which a transaction may only release locks. See [05](05-two-phase-locking-protocol.md).

**Starvation** — A transaction that could eventually proceed but keeps getting unlucky (e.g. repeatedly chosen as a victim), distinct from deadlock. See [06](06-deadlocks-explained.md).

**Strict 2PL** — A 2PL variant where all locks are held until commit/abort and released all at once; not what this project implements (this project implements basic 2PL). See [05](05-two-phase-locking-protocol.md).

**Thread** — An independent stream of execution sharing memory with its parent process; this project runs one thread per simulated transaction. See [03](03-concurrency-threads-and-race-conditions.md).

**Thundering herd** — The problem of many waiting threads all waking up and retrying at the exact same moment, causing renewed contention; mitigated here with randomized backoff jitter. See [08](08-wait-for-graphs-and-victim-selection.md).

**Transaction** — A group of operations treated as a single indivisible unit of work, ending in commit or abort. See [02](02-databases-and-transactions.md), [10](10-module-transaction.md).

**Victim (deadlock victim)** — The transaction chosen to be aborted in order to break a detected deadlock cycle. See [08](08-wait-for-graphs-and-victim-selection.md).

**Victim selection** — The policy for choosing which transaction in a deadlock cycle to abort; this project uses priority-based selection (lowest priority loses). See [08](08-wait-for-graphs-and-victim-selection.md).

**Wait-for graph** — A graph with only transaction nodes, where an edge means "this transaction is waiting on that one"; cycles indicate deadlock. See [08](08-wait-for-graphs-and-victim-selection.md).
