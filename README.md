# In-Memory Transactional Key-Value Store (C++)

A robust, header-only, templated in-memory Key-Value (KV) store implemented in modern C++ (C++17/C++20). Features support for nested transactions (`begin`, `commit`, `rollback`), deletion tombstones, RAII transaction scoping, thread-safe synchronization, and an interactive CLI / REPL.

---

## Features

- **Standard KV Operations**: `set(key, value)`, `get(key)`, `del(key)`, `contains(key)`.
- **Full Transaction Lifecycle**:
  - `begin()`: Starts a new transaction context. Supports arbitrary nesting levels.
  - `commit()`: Flushes changes to the parent transaction or the global store.
  - `rollback()`: Discards all mutations made within the active transaction in $O(1)$ time.
- **Tombstone Semantics**: Deletions inside a transaction properly shadow keys from outer transactions and the global store without prematurely mutating persistent data.
- **Read-Your-Own-Writes Isolation**: Reads reflect uncommitted writes and deletions within the current transaction hierarchy.
- **RAII Scoped Transactions**: `TransactionScope` automatically rolls back on early returns or exceptions unless explicitly committed.
- **Thread Safety Wrapper**: `ThreadSafeKVStore` provides reader-writer synchronization using `std::shared_mutex`.
- **Interactive CLI / REPL**: Command-line shell with dynamic prompts indicating the active transaction depth (e.g. `kvstore (tx:2)>`).
- **Zero External Dependencies**: Standard C++ Standard Library only.

---

## Architecture & Design

### Transaction Stack (Overlay / Shadow Paging Pattern)

The store maintains a committed baseline map (`global_store_`) and a transaction stack:

```
[ Global Store: { a: "1", b: "2" } ]
      │
      ▼
[ Transaction Layer 1: { b: "20", c: "30" } ]
      │
      ▼
[ Transaction Layer 2: { a: nullopt (tombstone), d: "40" } ]   <-- Active View
```

Each transaction layer is stored as:
```cpp
std::vector<std::unordered_map<Key, std::optional<Value>>> transaction_stack_;
```

- **Values (`std::optional<Value>(val)`)**: Represents an uncommitted insert or update in that transaction level.
- **Tombstones (`std::nullopt`)**: Represents an uncommitted deletion in that transaction level.

### Key Operations & Complexity

| Operation | Description | Time Complexity |
|---|---|---|
| `set(k, v)` | Writes to the innermost active transaction (or global store) | $O(1)$ average |
| `get(k)` | Inspects transaction stack top-down, falling back to global store | $O(D)$ where $D$ is transaction depth |
| `del(k)` | Places a tombstone in active transaction (or erases from global store) | $O(D)$ lookup + $O(1)$ write |
| `begin()` | Pushes a new transaction frame onto the stack | $O(1)$ amortized |
| `commit()` | Merges the top transaction into its parent layer or global store | $O(K)$ where $K$ is number of modified keys in that tx |
| `rollback()` | Discards the topmost transaction frame from the stack | $O(1)$ |

---

## Project Structure

```
KVStore/
├── include/
│   └── KVStore.hpp          # Core header-only KVStore, RAII guard, and ThreadSafeKVStore
├── src/
│   └── main.cpp             # Interactive REPL / CLI client
├── tests/
│   └── test_kvstore.cpp     # Comprehensive test suite (11 test suites)
├── CMakeLists.txt           # CMake build configuration
├── Makefile                 # Make build configuration
├── build.sh                 # Self-contained build script
└── README.md
```

---

## Building and Testing

### 1. Using `build.sh` (Linux / WSL / macOS)

```bash
chmod +x build.sh
./build.sh
```

This compiles both the CLI and test suite into `bin/` and executes the tests.

### 2. Using `make`

```bash
make          # Build CLI and test runner
make test     # Run all unit tests
make run      # Run interactive CLI
make clean    # Remove build artifacts
```

### 3. Using Direct `g++` (C++17 or newer)

```bash
# Build REPL
g++ -std=c++17 -Wall -Wextra -O2 -Iinclude src/main.cpp -o bin/kvstore_cli

# Build & Run Tests
g++ -std=c++17 -Wall -Wextra -O2 -pthread -Iinclude tests/test_kvstore.cpp -o bin/test_kvstore
./bin/test_kvstore
```

---

## C++ API Usage Example

### Basic & Transactional Usage

```cpp
#include "KVStore.hpp"
#include <iostream>

int main() {
    kv::KVStore<std::string, std::string> store;

    // Direct operations
    store.set("user:1", "Alice");
    std::cout << store.get("user:1").value_or("not found") << "\n"; // "Alice"

    // Transaction 1
    store.begin();
    store.set("user:1", "Bob");
    store.set("user:2", "Charlie");
    std::cout << store.get("user:1").value() << "\n"; // "Bob"

    // Nested Transaction 2
    store.begin();
    store.set("user:1", "Dave");
    store.del("user:2"); // Shadowed by tombstone
    std::cout << store.contains("user:2") << "\n"; // false

    // Rollback nested transaction
    store.rollback();
    std::cout << store.get("user:1").value() << "\n"; // "Bob"
    std::cout << store.contains("user:2") << "\n"; // true ("Charlie")

    // Commit outer transaction
    store.commit();
    std::cout << store.get("user:1").value() << "\n"; // "Bob"

    return 0;
}
```

### RAII Scoped Transaction

```cpp
{
    auto tx = store.transaction(); // Automatically begins transaction
    store.set("account", "1000");

    // If an exception occurs or scope exits without commit(),
    // the destructor automatically rolls back!
    tx.commit();
}
```

---

## Interactive REPL Walkthrough

Launch the CLI:
```bash
./bin/kvstore_cli
```

Example session:
```text
kvstore> SET name Alice
OK
kvstore> GET name
"Alice"
kvstore> BEGIN
Transaction started (depth: 1)
kvstore (tx:1)> SET name Bob
OK
kvstore (tx:1)> GET name
"Bob"
kvstore (tx:1)> BEGIN
Transaction started (depth: 2)
kvstore (tx:2)> SET name Charlie
OK
kvstore (tx:2)> GET name
"Charlie"
kvstore (tx:2)> COMMIT
Transaction committed. Current depth: 1
kvstore (tx:1)> GET name
"Charlie"
kvstore (tx:1)> ROLLBACK
Transaction rolled back. Current depth: 0
kvstore> GET name
"Alice"
kvstore> EXIT
```

---

## Unit Test Coverage

The test suite in [`tests/test_kvstore.cpp`](file:///tests/test_kvstore.cpp) includes:

1. `test_basic_crud`: Set, get, overwrite, delete, non-existent key handling.
2. `test_single_transaction_commit`: Updates and new keys committed to store.
3. `test_single_transaction_rollback`: Verification of complete state reversion.
4. `test_deletion_in_transaction`: Tombstones and rollback/commit of deleted keys.
5. `test_nested_transactions_all_commit`: Multi-level layer merging.
6. `test_nested_transactions_inner_rollback`: Inner abort leaves outer intact.
7. `test_nested_transactions_outer_rollback`: Outer abort discards committed inner changes.
8. `test_invalid_commit_and_rollback`: Graceful error handling with no active transactions.
9. `test_raii_transaction_scope`: Auto-rollback on scope exit and exception safety.
10. `test_keys_and_size_with_transactions`: Correct count and key sets under overlays and tombstones.
11. `test_thread_safe_store`: Concurrent multi-threaded reads and writes under `ThreadSafeKVStore`.
