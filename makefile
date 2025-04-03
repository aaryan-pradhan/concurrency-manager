all: 
	g++ -std=c++17 -o 2pl_test_runner src/2pl_test_runner.cpp src/concurrency_manager.cpp src/transaction.cpp src/lock_manager.cpp src/logger.cpp -I include/ -pthread
run : 
	./2pl_test_runner tests/test1.txt
clean:
	rm -f 2pl_test_runner*
	rm -f *.log