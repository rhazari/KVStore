#include "KVStore.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace {
// ANSI color helpers for enhanced interactive experience
const std::string RESET   = "\033[0m";
const std::string BOLD    = "\033[1m";
const std::string RED     = "\033[31m";
const std::string GREEN   = "\033[32m";
const std::string YELLOW  = "\033[33m";
const std::string BLUE    = "\033[34m";
const std::string CYAN    = "\033[36m";

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::vector<std::string> parse_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void print_help() {
    std::cout << BOLD << CYAN << "\nAvailable Commands:" << RESET << "\n"
              << "  " << GREEN << "SET <key> <value>" << RESET << "    : Set key to value\n"
              << "  " << GREEN << "GET <key>" << RESET << "            : Retrieve value for key\n"
              << "  " << GREEN << "DEL <key>" << RESET << "            : Delete key\n"
              << "  " << GREEN << "EXISTS <key>" << RESET << "         : Check if key exists\n"
              << "  " << GREEN << "KEYS" << RESET << "                 : List all visible keys\n"
              << "  " << GREEN << "COUNT" << RESET << "                : Number of active keys\n"
              << "  " << YELLOW << "BEGIN" << RESET << "                : Start a new transaction context\n"
              << "  " << YELLOW << "COMMIT" << RESET << "               : Commit active transaction changes\n"
              << "  " << YELLOW << "ROLLBACK" << RESET << "             : Discard active transaction changes\n"
              << "  " << BLUE << "DEPTH" << RESET << "                : Show current transaction depth\n"
              << "  " << BLUE << "CLEAR" << RESET << "                : Clear store and all transactions\n"
              << "  " << BLUE << "HELP" << RESET << "                 : Print this help message\n"
              << "  " << RED  << "EXIT / QUIT" << RESET << "          : Exit the program\n\n";
}
} // namespace

int main() {
    kv::KVStore<std::string, std::string> store;

    std::cout << BOLD << CYAN << "=================================================" << RESET << "\n";
    std::cout << BOLD << " In-Memory Transactional Key-Value Store (C++)  " << RESET << "\n";
    std::cout << " Type " << GREEN << "HELP" << RESET << " for commands or " 
              << RED << "EXIT" << RESET << " to quit." << "\n";
    std::cout << BOLD << CYAN << "=================================================" << RESET << "\n";

    std::string line;
    while (true) {
        // Dynamic prompt showing transaction depth
        size_t depth = store.transaction_depth();
        if (depth == 0) {
            std::cout << BOLD << "kvstore" << RESET << "> " << std::flush;
        } else {
            std::cout << BOLD << "kvstore (" << YELLOW << "tx:" << depth << RESET << BOLD << ")" << RESET << "> " << std::flush;
        }

        if (!std::getline(std::cin, line)) {
            break;
        }

        // Trim leading and trailing spaces
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        line = line.substr(start);

        auto tokens = parse_tokens(line);
        if (tokens.empty()) continue;

        std::string cmd = to_upper(tokens[0]);

        if (cmd == "EXIT" || cmd == "QUIT") {
            std::cout << "Bye!\n";
            break;
        } else if (cmd == "HELP") {
            print_help();
        } else if (cmd == "BEGIN") {
            size_t new_depth = store.begin();
            std::cout << YELLOW << "Transaction started (depth: " << new_depth << ")" << RESET << "\n";
        } else if (cmd == "COMMIT") {
            if (store.commit()) {
                std::cout << GREEN << "Transaction committed. Current depth: " << store.transaction_depth() << RESET << "\n";
            } else {
                std::cout << RED << "ERROR: No active transaction to commit." << RESET << "\n";
            }
        } else if (cmd == "ROLLBACK") {
            if (store.rollback()) {
                std::cout << YELLOW << "Transaction rolled back. Current depth: " << store.transaction_depth() << RESET << "\n";
            } else {
                std::cout << RED << "ERROR: No active transaction to rollback." << RESET << "\n";
            }
        } else if (cmd == "DEPTH") {
            std::cout << "Transaction depth: " << store.transaction_depth() << "\n";
        } else if (cmd == "COUNT" || cmd == "SIZE") {
            std::cout << "Count: " << store.size() << "\n";
        } else if (cmd == "CLEAR") {
            store.clear();
            std::cout << "Store cleared.\n";
        } else if (cmd == "KEYS") {
            auto keys = store.keys();
            std::sort(keys.begin(), keys.end());
            if (keys.empty()) {
                std::cout << "(empty)\n";
            } else {
                std::cout << "[" << keys.size() << " keys]:\n";
                for (const auto& k : keys) {
                    std::cout << "  - " << k << "\n";
                }
            }
        } else if (cmd == "GET") {
            if (tokens.size() < 2) {
                std::cout << RED << "Usage: GET <key>" << RESET << "\n";
            } else {
                const auto& key = tokens[1];
                auto val = store.get(key);
                if (val.has_value()) {
                    std::cout << GREEN << "\"" << *val << "\"" << RESET << "\n";
                } else {
                    std::cout << RED << "(nil)" << RESET << "\n";
                }
            }
        } else if (cmd == "EXISTS") {
            if (tokens.size() < 2) {
                std::cout << RED << "Usage: EXISTS <key>" << RESET << "\n";
            } else {
                const auto& key = tokens[1];
                std::cout << (store.contains(key) ? "true (1)" : "false (0)") << "\n";
            }
        } else if (cmd == "SET") {
            if (tokens.size() < 3) {
                std::cout << RED << "Usage: SET <key> <value>" << RESET << "\n";
            } else {
                const std::string& key = tokens[1];
                // Support multi-word values: find position after key in the original line
                auto key_pos = line.find(key);
                auto val_pos = line.find_first_not_of(" \t", key_pos + key.length());
                std::string val = (val_pos != std::string::npos) ? line.substr(val_pos) : tokens[2];
                store.set(key, val);
                std::cout << GREEN << "OK" << RESET << "\n";
            }
        } else if (cmd == "DEL" || cmd == "DELETE") {
            if (tokens.size() < 2) {
                std::cout << RED << "Usage: DEL <key>" << RESET << "\n";
            } else {
                const auto& key = tokens[1];
                if (store.del(key)) {
                    std::cout << GREEN << "(integer) 1" << RESET << "\n";
                } else {
                    std::cout << "(integer) 0 (key not found)\n";
                }
            }
        } else {
            std::cout << RED << "Unknown command: " << tokens[0] << ". Type HELP for instructions." << RESET << "\n";
        }
    }

    return 0;
}
