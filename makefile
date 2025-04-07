run: deadlock_test_runner
	./deadlock_test tests/test4.txt

deadlock_test_runner: tests/test_deadlock_detection.cpp src/concurrency_manager.cpp src/lock_manager.cpp src/deadlock_detector.cpp src/transaction.cpp src/logger.cpp src/resource_manager.cpp
	g++ -std=c++17 -o deadlock_test tests/test_deadlock_detection.cpp src/concurrency_manager.cpp src/lock_manager.cpp src/deadlock_detector.cpp src/transaction.cpp src/logger.cpp src/resource_manager.cpp -pthread

clean:
	rm -f 2pl_test_runner* deadlock_test*
	rm -f *.log