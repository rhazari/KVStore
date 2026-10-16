#pragma once

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <string>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <utility>

namespace kv {

/**
 * @brief Transactional In-Memory Key-Value Store.
 *
 * Supports nested transactions with begin, commit, and rollback semantics.
 * Implements an overlay/shadow stack approach with tombstones for O(1) rollbacks
 * and proper read-your-own-writes transaction isolation.
 *
 * @tparam Key The key type (defaults to std::string)
 * @tparam Value The value type (defaults to std::string)
 * @tparam Hash Hash functor for Key
 * @tparam KeyEqual Equality functor for Key
 */
template <
    typename Key = std::string,
    typename Value = std::string,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>
>
class KVStore {
public:
    // Forward declaration of RAII Transaction Scope Guard
    class TransactionScope;

    KVStore() = default;
    ~KVStore() = default;

    // Non-copyable, movable
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;
    KVStore(KVStore&&) noexcept = default;
    KVStore& operator=(KVStore&&) noexcept = default;

    /**
     * @brief Sets key to value in the active context (current transaction or global store).
     * @param key The key to set.
     * @param value The value to associate with key.
     */
    void set(const Key& key, Value value) {
        if (transaction_stack_.empty()) {
            global_store_[key] = std::move(value);
        } else {
            // Write to the innermost active transaction
            transaction_stack_.back()[key] = std::move(value);
        }
    }

    /**
     * @brief Retrieves the value associated with key, if it exists.
     * Searches top-down from the newest transaction layer to the global store.
     * Respects tombstones (deleted keys) in active transactions.
     * @param key The key to search for.
     * @return std::optional containing the value if found, or std::nullopt.
     */
    [[nodiscard]] std::optional<Value> get(const Key& key) const {
        // 1. Check transaction stack from top (innermost) to bottom (outermost)
        for (auto it = transaction_stack_.rbegin(); it != transaction_stack_.rend(); ++it) {
            auto found = it->find(key);
            if (found != it->end()) {
                // If it has a value, return it. If it is nullopt (tombstone), key is deleted.
                return found->second;
            }
        }

        // 2. Fall back to global base store
        auto found = global_store_.find(key);
        if (found != global_store_.end()) {
            return found->second;
        }

        return std::nullopt;
    }

    /**
     * @brief Checks if a key exists in the current view of the store.
     * @param key The key to check.
     * @return true if the key exists, false otherwise.
     */
    [[nodiscard]] bool contains(const Key& key) const {
        return get(key).has_value();
    }

    /**
     * @brief Deletes a key from the active context.
     * If inside a transaction, a tombstone (std::nullopt) is recorded so parent
     * layers or global store values are shadowed.
     * @param key The key to delete.
     * @return true if the key existed and was deleted, false if key did not exist.
     */
    bool del(const Key& key) {
        // Key must exist in current view to be deleted
        if (!contains(key)) {
            return false;
        }

        if (transaction_stack_.empty()) {
            return global_store_.erase(key) > 0;
        } else {
            // Write a tombstone to current transaction level
            transaction_stack_.back()[key] = std::nullopt;
            return true;
        }
    }

    /**
     * @brief Starts a new transaction context.
     * Can be nested to arbitrary depths.
     * @return The new transaction depth.
     */
    size_t begin() {
        transaction_stack_.emplace_back();
        return transaction_stack_.size();
    }

    /**
     * @brief Commits the most recent transaction.
     * - If nested, merges changes into parent transaction layer.
     * - If root transaction, persists changes to the global store.
     * @return true if a transaction was committed, false if no transaction was active.
     */
    bool commit() {
        if (transaction_stack_.empty()) {
            return false;
        }

        // Take ownership of the top transaction
        auto top_tx = std::move(transaction_stack_.back());
        transaction_stack_.pop_back();

        if (transaction_stack_.empty()) {
            // Commit to global store
            for (auto& [key, opt_val] : top_tx) {
                if (opt_val.has_value()) {
                    global_store_[std::move(key)] = std::move(*opt_val);
                } else {
                    global_store_.erase(key);
                }
            }
        } else {
            // Merge into parent transaction layer
            auto& parent_tx = transaction_stack_.back();
            for (auto& [key, opt_val] : top_tx) {
                parent_tx[std::move(key)] = std::move(opt_val);
            }
        }

        return true;
    }

    /**
     * @brief Rolls back (aborts) the most recent transaction, discarding all its uncommitted modifications.
     * @return true if a transaction was rolled back, false if no transaction was active.
     */
    bool rollback() {
        if (transaction_stack_.empty()) {
            return false;
        }

        // Simply popping the top transaction layer reverts all writes made in it
        transaction_stack_.pop_back();
        return true;
    }

    /**
     * @brief Returns the current transaction nesting level (0 = outside transaction).
     */
    [[nodiscard]] size_t transaction_depth() const noexcept {
        return transaction_stack_.size();
    }

    /**
     * @brief Returns whether a transaction is currently in progress.
     */
    [[nodiscard]] bool in_transaction() const noexcept {
        return !transaction_stack_.empty();
    }

    /**
     * @brief Returns the number of distinct keys visible in the current view.
     */
    [[nodiscard]] size_t size() const {
        return keys().size();
    }

    /**
     * @brief Checks if the store is currently empty in the visible context.
     */
    [[nodiscard]] bool empty() const {
        return size() == 0;
    }

    /**
     * @brief Returns a list of all currently visible keys.
     */
    [[nodiscard]] std::vector<Key> keys() const {
        std::unordered_set<Key, Hash, KeyEqual> visible_keys;
        std::unordered_set<Key, Hash, KeyEqual> deleted_keys;

        // Traverse transaction stack from top to bottom
        for (auto it = transaction_stack_.rbegin(); it != transaction_stack_.rend(); ++it) {
            for (const auto& [k, opt_val] : *it) {
                if (!visible_keys.count(k) && !deleted_keys.count(k)) {
                    if (opt_val.has_value()) {
                        visible_keys.insert(k);
                    } else {
                        deleted_keys.insert(k);
                    }
                }
            }
        }

        // Add remaining keys from global store
        for (const auto& [k, _] : global_store_) {
            if (!visible_keys.count(k) && !deleted_keys.count(k)) {
                visible_keys.insert(k);
            }
        }

        return std::vector<Key>(visible_keys.begin(), visible_keys.end());
    }

    /**
     * @brief Clears all transactions and global data.
     */
    void clear() {
        transaction_stack_.clear();
        global_store_.clear();
    }

    /**
     * @brief Creates an RAII transaction scope.
     * Automatically rolls back upon exiting scope if not committed.
     * @return TransactionScope instance.
     */
    TransactionScope transaction() {
        return TransactionScope(*this);
    }

    /**
     * @brief RAII Transaction Scope guard.
     */
    class TransactionScope {
    public:
        explicit TransactionScope(KVStore& store)
            : store_(store), active_(true) {
            store_.begin();
        }

        ~TransactionScope() {
            if (active_) {
                store_.rollback();
            }
        }

        // Non-copyable
        TransactionScope(const TransactionScope&) = delete;
        TransactionScope& operator=(const TransactionScope&) = delete;

        // Movable
        TransactionScope(TransactionScope&& other) noexcept
            : store_(other.store_), active_(other.active_) {
            other.active_ = false;
        }

        TransactionScope& operator=(TransactionScope&& other) noexcept {
            if (this != &other) {
                if (active_) {
                    store_.rollback();
                }
                store_ = other.store_;
                active_ = other.active_;
                other.active_ = false;
            }
            return *this;
        }

        /**
         * @brief Commits the transaction associated with this scope.
         * @return true on success, false if already finalized.
         */
        bool commit() {
            if (!active_) return false;
            active_ = false;
            return store_.commit();
        }

        /**
         * @brief Explicitly rolls back the transaction associated with this scope.
         * @return true on success, false if already finalized.
         */
        bool rollback() {
            if (!active_) return false;
            active_ = false;
            return store_.rollback();
        }

        [[nodiscard]] bool is_active() const noexcept {
            return active_;
        }

    private:
        KVStore& store_;
        bool active_{false};
    };

private:
    // Global base storage (committed data)
    std::unordered_map<Key, Value, Hash, KeyEqual> global_store_;

    // Transaction layers stack.
    // Each layer maps Key -> std::optional<Value>.
    // - opt_val.has_value(): key inserted/updated in this transaction level.
    // - !opt_val.has_value(): key deleted in this transaction level (tombstone).
    std::vector<std::unordered_map<Key, std::optional<Value>, Hash, KeyEqual>> transaction_stack_;
};

/**
 * @brief Thread-safe wrapper for KVStore using a shared_mutex (Reader-Writer lock).
 */
template <
    typename Key = std::string,
    typename Value = std::string,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>
>
class ThreadSafeKVStore {
public:
    ThreadSafeKVStore() = default;

    void set(const Key& key, Value value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        store_.set(key, std::move(value));
    }

    [[nodiscard]] std::optional<Value> get(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return store_.get(key);
    }

    [[nodiscard]] bool contains(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return store_.contains(key);
    }

    bool del(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return store_.del(key);
    }

    size_t begin() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return store_.begin();
    }

    bool commit() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return store_.commit();
    }

    bool rollback() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return store_.rollback();
    }

    [[nodiscard]] size_t transaction_depth() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return store_.transaction_depth();
    }

    [[nodiscard]] bool in_transaction() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return store_.in_transaction();
    }

    [[nodiscard]] size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return store_.size();
    }

    [[nodiscard]] std::vector<Key> keys() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return store_.keys();
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        store_.clear();
    }

private:
    KVStore<Key, Value, Hash, KeyEqual> store_;
    mutable std::shared_mutex mutex_;
};

} // namespace kv
