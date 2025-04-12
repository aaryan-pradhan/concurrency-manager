import random

def generate_large_deadlock_heavy_workload(
    num_transactions=1000,
    batch_size=10,
    ops_per_transaction=4,
    data_items_pool=["A", "B", "C", "D", "E"],
    output_file="deadlock_1000_transactions.txt"
):
    with open(output_file, "w") as f:
        transactions = {}
        current_txn = 1

        while current_txn <= num_transactions:
            # Choose a small set of data items for this batch
            data_items = random.sample(data_items_pool, k=3)
            for i in range(batch_size):
                if current_txn > num_transactions:
                    break

                f.write(f"START T{current_txn}\n")
                access_order = data_items if i % 2 == 0 else data_items[::-1]
                tx_ops = []

                # First half: read (shared locks)
                for item in access_order[:ops_per_transaction // 2]:
                    tx_ops.append(f"R{current_txn}({item})")

                # Second half: write (exclusive locks, may need upgrade)
                for item in access_order[ops_per_transaction // 2:]:
                    tx_ops.append(f"W{current_txn}({item})")

                transactions[current_txn] = tx_ops
                current_txn += 1

        # Interleave all transactions
        max_ops = max(len(tx) for tx in transactions.values())
        for op_index in range(max_ops):
            for tid in sorted(transactions.keys()):
                if op_index < len(transactions[tid]):
                    f.write(transactions[tid][op_index] + "\n")

        # Add commit lines
        for tid in sorted(transactions.keys()):
            f.write(f"C{tid}\n\n")

    print(f"Generated {num_transactions} deadlock-heavy transactions in {output_file}")

# CONFIGURATION
if __name__ == "__main__":
    generate_large_deadlock_heavy_workload(
        num_transactions=1000,
        batch_size=10,                   # Each batch forms a potential deadlock ring
        ops_per_transaction=4,
        data_items_pool=list("ABCDEFGHIJKL"),
        output_file="dead_test.txt"
    )
