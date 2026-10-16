#include "KVStore.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] Assertion FAILED: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

void test_basic_crud() {
    std::cout << "[RUNNING] test_basic_crud..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    TEST_ASSERT(store.size() == 0, "Initial size should be 0");
    TEST_ASSERT(store.empty(), "Initial store should be empty");
    TEST_ASSERT(!store.in_transaction(), "Should not be in transaction");

    // Set & Get
    store.set("user", "alice");
    TEST_ASSERT(store.size() == 1, "Size should be 1 after set");
    TEST_ASSERT(!store.empty(), "Store should not be empty");
    TEST_ASSERT(store.contains("user"), "Contains should return true");
    auto val = store.get("user");
    TEST_ASSERT(val.has_value() && *val == "alice", "Get should return alice");

    // Overwrite
    store.set("user", "bob");
    val = store.get("user");
    TEST_ASSERT(val.has_value() && *val == "bob", "Get should return updated bob");

    // Delete
    bool del_res = store.del("user");
    TEST_ASSERT(del_res, "del() should return true for existing key");
    TEST_ASSERT(!store.contains("user"), "Contains should return false after deletion");
    TEST_ASSERT(!store.get("user").has_value(), "Get should return nullopt after deletion");
    TEST_ASSERT(store.size() == 0, "Size should be 0 after delete");

    // Delete non-existent key
    del_res = store.del("non_existent");
    TEST_ASSERT(!del_res, "del() should return false for non-existent key");

    std::cout << "[PASSED] test_basic_crud" << std::endl;
}

void test_single_transaction_commit() {
    std::cout << "[RUNNING] test_single_transaction_commit..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    store.set("k1", "v1");

    size_t depth = store.begin();
    TEST_ASSERT(depth == 1, "Transaction depth should be 1");
    TEST_ASSERT(store.in_transaction(), "Should be in transaction");

    store.set("k2", "v2");
    store.set("k1", "v1_updated");

    // Reads within transaction
    TEST_ASSERT(store.get("k1").value() == "v1_updated", "k1 should be updated in tx");
    TEST_ASSERT(store.get("k2").value() == "v2", "k2 should be visible in tx");

    bool commit_res = store.commit();
    TEST_ASSERT(commit_res, "commit() should succeed");
    TEST_ASSERT(!store.in_transaction(), "Should no longer be in transaction");
    TEST_ASSERT(store.transaction_depth() == 0, "Depth should be 0");

    // Reads after commit
    TEST_ASSERT(store.get("k1").value() == "v1_updated", "k1 should remain updated");
    TEST_ASSERT(store.get("k2").value() == "v2", "k2 should persist");
    TEST_ASSERT(store.size() == 2, "Size should be 2");

    std::cout << "[PASSED] test_single_transaction_commit" << std::endl;
}

void test_single_transaction_rollback() {
    std::cout << "[RUNNING] test_single_transaction_rollback..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    store.set("k1", "v1");

    store.begin();
    store.set("k2", "v2");
    store.set("k1", "v1_modified");

    TEST_ASSERT(store.get("k1").value() == "v1_modified", "Modified k1 in tx");
    TEST_ASSERT(store.get("k2").value() == "v2", "New k2 in tx");

    bool rb_res = store.rollback();
    TEST_ASSERT(rb_res, "rollback() should succeed");
    TEST_ASSERT(!store.in_transaction(), "Should no longer be in transaction");

    // After rollback, k1 should be restored to "v1" and k2 should not exist
    TEST_ASSERT(store.get("k1").value() == "v1", "k1 should be restored to v1");
    TEST_ASSERT(!store.contains("k2"), "k2 should not exist after rollback");
    TEST_ASSERT(store.size() == 1, "Size should be 1");

    std::cout << "[PASSED] test_single_transaction_rollback" << std::endl;
}

void test_deletion_in_transaction() {
    std::cout << "[RUNNING] test_deletion_in_transaction..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    store.set("item", "lamp");

    // Test rollback of delete
    store.begin();
    bool del_res = store.del("item");
    TEST_ASSERT(del_res, "del() should succeed inside transaction");
    TEST_ASSERT(!store.contains("item"), "item should be shadowed/tombstoned in tx");
    TEST_ASSERT(!store.get("item").has_value(), "get(item) should return nullopt");

    store.rollback();
    TEST_ASSERT(store.contains("item"), "item should be restored after rollback");
    TEST_ASSERT(store.get("item").value() == "lamp", "item value should be lamp");

    // Test commit of delete
    store.begin();
    store.del("item");
    store.commit();
    TEST_ASSERT(!store.contains("item"), "item should be permanently deleted after commit");
    TEST_ASSERT(store.size() == 0, "Store should be empty");

    std::cout << "[PASSED] test_deletion_in_transaction" << std::endl;
}

void test_nested_transactions_all_commit() {
    std::cout << "[RUNNING] test_nested_transactions_all_commit..." << std::endl;
    kv::KVStore<std::string, int> store;

    store.set("x", 100);

    store.begin(); // Level 1
    store.set("x", 200);
    store.set("y", 500);

    store.begin(); // Level 2
    store.set("x", 300);
    store.set("z", 700);

    TEST_ASSERT(store.get("x").value() == 300, "Level 2 sees x=300");
    TEST_ASSERT(store.get("y").value() == 500, "Level 2 sees y=500 from Level 1");
    TEST_ASSERT(store.get("z").value() == 700, "Level 2 sees z=700");

    TEST_ASSERT(store.commit(), "Commit level 2 should succeed");
    TEST_ASSERT(store.transaction_depth() == 1, "Depth should now be 1");
    // Level 1 now contains merged changes from level 2
    TEST_ASSERT(store.get("x").value() == 300, "Level 1 sees merged x=300");
    TEST_ASSERT(store.get("z").value() == 700, "Level 1 sees merged z=700");

    TEST_ASSERT(store.commit(), "Commit level 1 should succeed");
    TEST_ASSERT(store.transaction_depth() == 0, "Depth should now be 0");
    TEST_ASSERT(store.get("x").value() == 300, "Global store sees x=300");
    TEST_ASSERT(store.get("y").value() == 500, "Global store sees y=500");
    TEST_ASSERT(store.get("z").value() == 700, "Global store sees z=700");

    std::cout << "[PASSED] test_nested_transactions_all_commit" << std::endl;
}

void test_nested_transactions_inner_rollback() {
    std::cout << "[RUNNING] test_nested_transactions_inner_rollback..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    store.set("a", "alpha");

    store.begin(); // Level 1
    store.set("a", "alpha_L1");
    store.set("b", "beta_L1");

    store.begin(); // Level 2
    store.set("a", "alpha_L2");
    store.set("c", "gamma_L2");

    TEST_ASSERT(store.get("a").value() == "alpha_L2", "L2 sees a=alpha_L2");

    store.rollback(); // Rollback Level 2
    TEST_ASSERT(store.transaction_depth() == 1, "Depth should be 1 after L2 rollback");
    TEST_ASSERT(store.get("a").value() == "alpha_L1", "L1 sees a=alpha_L1 after L2 rollback");
    TEST_ASSERT(!store.contains("c"), "c from L2 should no longer exist");
    TEST_ASSERT(store.get("b").value() == "beta_L1", "b from L1 still exists");

    store.commit(); // Commit Level 1
    TEST_ASSERT(store.get("a").value() == "alpha_L1", "Global store has a=alpha_L1");
    TEST_ASSERT(store.get("b").value() == "beta_L1", "Global store has b=beta_L1");
    TEST_ASSERT(!store.contains("c"), "Global store does not have c");

    std::cout << "[PASSED] test_nested_transactions_inner_rollback" << std::endl;
}

void test_nested_transactions_outer_rollback() {
    std::cout << "[RUNNING] test_nested_transactions_outer_rollback..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    store.set("k", "original");

    store.begin(); // L1
    store.set("k", "tx1_val");

    store.begin(); // L2
    store.set("k", "tx2_val");
    store.set("inner_only", "inner");
    store.commit(); // Commit L2 into L1!

    TEST_ASSERT(store.get("k").value() == "tx2_val", "L1 now holds tx2_val");
    TEST_ASSERT(store.get("inner_only").value() == "inner", "L1 holds inner_only");

    // Now rollback L1! Even though L2 committed into L1, rolling back L1 must revert EVERYTHING!
    store.rollback();
    TEST_ASSERT(store.transaction_depth() == 0, "Depth is 0");
    TEST_ASSERT(store.get("k").value() == "original", "k reverted to original");
    TEST_ASSERT(!store.contains("inner_only"), "inner_only discarded by outer rollback");

    std::cout << "[PASSED] test_nested_transactions_outer_rollback" << std::endl;
}

void test_invalid_commit_and_rollback() {
    std::cout << "[RUNNING] test_invalid_commit_and_rollback..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    TEST_ASSERT(!store.commit(), "commit() without active tx must return false");
    TEST_ASSERT(!store.rollback(), "rollback() without active tx must return false");

    store.begin();
    TEST_ASSERT(store.commit(), "First commit should succeed");
    TEST_ASSERT(!store.commit(), "Second commit must return false");
    TEST_ASSERT(!store.rollback(), "Rollback after commit must return false");

    std::cout << "[PASSED] test_invalid_commit_and_rollback" << std::endl;
}

void test_raii_transaction_scope() {
    std::cout << "[RUNNING] test_raii_transaction_scope..." << std::endl;
    kv::KVStore<std::string, int> store;
    store.set("counter", 10);

    // Case 1: Scope exits without commit -> Auto Rollback
    {
        auto tx = store.transaction();
        store.set("counter", 20);
        store.set("temp", 999);
        TEST_ASSERT(store.get("counter").value() == 20, "Should see 20 in scope");
        // tx goes out of scope here without commit() -> auto rollback!
    }
    TEST_ASSERT(store.get("counter").value() == 10, "Auto-rollback should revert counter to 10");
    TEST_ASSERT(!store.contains("temp"), "Auto-rollback should discard temp");

    // Case 2: Explicit commit within scope
    {
        auto tx = store.transaction();
        store.set("counter", 30);
        bool res = tx.commit();
        TEST_ASSERT(res, "tx.commit() should succeed");
    }
    TEST_ASSERT(store.get("counter").value() == 30, "counter should be 30 after commit");

    // Case 3: Exception safety auto-rollback
    try {
        auto tx = store.transaction();
        store.set("counter", 50);
        throw std::runtime_error("Simulated failure");
    } catch (const std::exception&) {
        // Exception caught, tx destructor ran and rolled back
    }
    TEST_ASSERT(store.get("counter").value() == 30, "Counter should be 30 after exception rollback");

    std::cout << "[PASSED] test_raii_transaction_scope" << std::endl;
}

void test_keys_and_size_with_transactions() {
    std::cout << "[RUNNING] test_keys_and_size_with_transactions..." << std::endl;
    kv::KVStore<std::string, std::string> store;

    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");

    TEST_ASSERT(store.size() == 3, "Size should be 3");

    store.begin();
    store.del("b");
    store.set("d", "4");

    // Visible keys should now be {a, c, d} (size 3)
    TEST_ASSERT(store.size() == 3, "Size should be 3 (deleted b, added d)");
    auto klist = store.keys();
    std::sort(klist.begin(), klist.end());
    std::vector<std::string> expected = {"a", "c", "d"};
    TEST_ASSERT(klist == expected, "Visible keys list should match {a, c, d}");

    store.rollback();
    TEST_ASSERT(store.size() == 3, "Size should be 3 after rollback");
    klist = store.keys();
    std::sort(klist.begin(), klist.end());
    expected = {"a", "b", "c"};
    TEST_ASSERT(klist == expected, "Visible keys list should match {a, b, c} after rollback");

    std::cout << "[PASSED] test_keys_and_size_with_transactions" << std::endl;
}

void test_thread_safe_store() {
    std::cout << "[RUNNING] test_thread_safe_store..." << std::endl;
    kv::ThreadSafeKVStore<int, int> store;

    const int num_threads = 8;
    const int ops_per_thread = 500;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&store, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                int key = t * 1000 + i;
                store.set(key, i);
                auto val = store.get(key);
                TEST_ASSERT(val.has_value() && *val == i, "Concurrent read should match write");
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    TEST_ASSERT(store.size() == static_cast<size_t>(num_threads * ops_per_thread),
                "Total size should match num_threads * ops_per_thread");
    std::cout << "[PASSED] test_thread_safe_store" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running KVStore Transaction Test Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    test_basic_crud();
    test_single_transaction_commit();
    test_single_transaction_rollback();
    test_deletion_in_transaction();
    test_nested_transactions_all_commit();
    test_nested_transactions_inner_rollback();
    test_nested_transactions_outer_rollback();
    test_invalid_commit_and_rollback();
    test_raii_transaction_scope();
    test_keys_and_size_with_transactions();
    test_thread_safe_store();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL TESTS PASSED SUCCESSFULLY! (11/11) " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

