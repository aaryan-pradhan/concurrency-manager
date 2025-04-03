run: 2pl_test_runner
	./2pl_test_runner tests/test1.txt
2pl_test_runner: src/2pl_test_runner.cpp src/concurrency_manager.cpp src/transaction.cpp src/lock_manager.cpp src/logger.cpp
	g++ -std=c++17 -o 2pl_test_runner src/2pl_test_runner.cpp src/concurrency_manager.cpp src/transaction.cpp src/lock_manager.cpp src/logger.cpp -I include/ -pthread
clean:
	rm -f 2pl_test_runner*
	rm -f *.log