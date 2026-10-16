#!/usr/bin/env bash
set -e

mkdir -p bin

echo "Compiling CLI..."
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude src/main.cpp -o bin/kvstore_cli

echo "Compiling Tests..."
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -pthread -Iinclude tests/test_kvstore.cpp -o bin/test_kvstore

echo "Running Tests..."
./bin/test_kvstore

echo ""
echo "Build succeeded! Executables are in ./bin/"
echo "Run interactive REPL with: ./bin/kvstore_cli"
