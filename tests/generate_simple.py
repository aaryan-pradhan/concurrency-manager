import random
import string

def generate_transactions(
    num_transactions=1000,
    min_ops=3,
    max_ops=8,
    read_ratio=0.7,
    data_items_pool="ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    output_file="generated_transactions.txt"
):
    with open(output_file, "w") as f:
        for t_id in range(1, num_transactions + 1):
            f.write(f"START T{t_id}\n")

            num_ops = random.randint(min_ops, max_ops)
            for _ in range(num_ops):
                op_type = "R" if random.random() < read_ratio else "W"
                item = random.choice(data_items_pool)
                f.write(f"{op_type}{t_id}({item})\n")

            f.write(f"C{t_id}\n\n")

    print(f"{num_transactions} transactions written to {output_file}")

# CONFIGURATION
if __name__ == "__main__":
    generate_transactions(
        num_transactions=1000,        # total number of transactions
        min_ops=3,                    # minimum operations per transaction
        max_ops=4,                    # maximum operations per transaction
        read_ratio=0.8,              # 70% reads, 30% writes
        data_items_pool="ABCDEFGHIJKLMNOPQRSTUVWXYZ",      # use fewer items to induce contention
        output_file="test.txt"
    )
