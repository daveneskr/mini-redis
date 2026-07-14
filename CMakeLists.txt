cmake_minimum_required(VERSION 3.20)

project(mini_redis C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -----------------------------
# Main Mini Redis library
# -----------------------------
add_library(mini_redis_core
        src/mini_redis.c
        src/store.c
        src/resp.c
        src/command.c
)

target_include_directories(mini_redis_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# -----------------------------
# Main executable
# -----------------------------
add_executable(mini_redis
        src/main.c
)

target_link_libraries(mini_redis
        PRIVATE
        mini_redis_core
)

# -----------------------------
# Unity
# -----------------------------
include(FetchContent)

FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG master
)

FetchContent_MakeAvailable(unity)

# -----------------------------
# CTest
# -----------------------------
enable_testing()

# -----------------------------
# Test executable
# -----------------------------
add_executable(test_smoke
        tests/test_smoke.c
)

target_link_libraries(test_smoke
        PRIVATE
        mini_redis_core
        unity
)

add_test(
        NAME smoke_test
        COMMAND test_smoke
)

add_executable(test_store
        tests/test_store.c
)

target_link_libraries(test_store
        PUBLIC
        mini_redis_core
        unity
)

target_compile_definitions(unity
        PUBLIC
        UNITY_INCLUDE_DOUBLE
)

add_test(
        NAME store_test
        COMMAND test_store
)

add_executable(test_resp
        tests/test_resp.c
)

target_link_libraries(test_resp
        PRIVATE
        mini_redis_core
        unity
)

add_test(
        NAME resp_test
        COMMAND test_resp
)

add_executable(test_command
        tests/test_command.c
)

target_link_libraries(test_command
        PRIVATE
        mini_redis_core
        unity
)

add_test(
        NAME command_test
        COMMAND test_command
)