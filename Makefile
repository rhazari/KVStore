CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude
LDFLAGS = -pthread

BIN_DIR = bin
CLI_TARGET = $(BIN_DIR)/kvstore_cli
TEST_TARGET = $(BIN_DIR)/test_kvstore

all: $(CLI_TARGET) $(TEST_TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(CLI_TARGET): src/main.cpp include/KVStore.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(CLI_TARGET)

$(TEST_TARGET): tests/test_kvstore.cpp include/KVStore.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) tests/test_kvstore.cpp -o $(TEST_TARGET) $(LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(CLI_TARGET)
	./$(CLI_TARGET)

clean:
	rm -rf $(BIN_DIR) test_runner kvstore_cli

.PHONY: all test run clean

