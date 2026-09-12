CORE = src/concurrency_manager.cpp src/lock_manager.cpp src/deadlock_detector.cpp src/transaction.cpp src/logger.cpp src/resource_manager.cpp
HEADERS = $(wildcard include/*.h)
CXXFLAGS = -std=c++23 -Wall -Wextra -pthread

run: deadlock_test_runner
	./deadlock_test tests/test4.txt

deadlock_test_runner: tests/test_deadlock_detection.cpp $(CORE) $(HEADERS)
	g++ $(CXXFLAGS) -o deadlock_test tests/test_deadlock_detection.cpp $(CORE)

2pl_test_runner: src/2pl_test_runner.cpp $(CORE) $(HEADERS)
	g++ $(CXXFLAGS) -o 2pl_test_runner src/2pl_test_runner.cpp $(CORE)

test_lock_manager: tests/test_lock_manager.cpp $(CORE) $(HEADERS)
	g++ $(CXXFLAGS) -o test_lock_manager tests/test_lock_manager.cpp src/lock_manager.cpp src/transaction.cpp src/logger.cpp src/resource_manager.cpp

test_correctness: tests/test_correctness.cpp $(CORE) $(HEADERS)
	g++ $(CXXFLAGS) -o test_correctness tests/test_correctness.cpp $(CORE)

benchmark: tests/benchmark.cpp $(CORE) $(HEADERS)
	g++ $(CXXFLAGS) -O2 -o benchmark tests/benchmark.cpp $(CORE)

# Unit tests, regression tests, and a small serializability-checked benchmark run
test: test_lock_manager test_correctness benchmark
	./test_lock_manager | grep -E "PASSED|FAILED"
	./test_correctness
	./benchmark --txns 300 --threads 1,8,64 --items 20 --write-ratio 0.3 --work-us 200 --runs 2 --label check

bench: benchmark
	bash tests/run_benchmarks.sh

clean:
	rm -f 2pl_test_runner* deadlock_test* test_lock_manager test_correctness benchmark
	rm -f *.log
