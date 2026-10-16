cmake_minimum_required(VERSION 3.15)
project(KVStore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

# CLI Executable
add_executable(kvstore_cli src/main.cpp)
target_include_directories(kvstore_cli PRIVATE include)
target_compile_options(kvstore_cli PRIVATE -Wall -Wextra -pedantic)

# Unit Test Executable
add_executable(test_kvstore tests/test_kvstore.cpp)
target_include_directories(test_kvstore PRIVATE include)
target_compile_options(test_kvstore PRIVATE -Wall -Wextra -pedantic -pthread)
target_link_libraries(test_kvstore PRIVATE pthread)

enable_testing()
add_test(NAME KVStoreTests COMMAND test_kvstore)
