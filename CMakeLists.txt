cmake_minimum_required(VERSION 3.20)

project(mini_redis C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(enable_project_warnings target_name)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    endif()
endfunction()

# -----------------------------
# Main Mini Redis library
# -----------------------------
add_library(mini_redis_core
        src/mini_redis.c
        src/server.c
        src/store.c
        src/resp.c
        src/command.c
)

target_include_directories(mini_redis_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_project_warnings(mini_redis_core)

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

enable_project_warnings(mini_redis)

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

target_compile_definitions(unity
        PUBLIC
        UNITY_INCLUDE_DOUBLE
)

# -----------------------------
# CTest
# -----------------------------
enable_testing()

function(add_mini_redis_test target_name test_source test_name)
    add_executable(${target_name} ${test_source})
    target_link_libraries(${target_name}
            PRIVATE
            mini_redis_core
            unity
    )
    enable_project_warnings(${target_name})
    add_test(NAME ${test_name} COMMAND ${target_name})
endfunction()

add_mini_redis_test(test_smoke tests/test_smoke.c smoke_test)
add_mini_redis_test(test_store tests/test_store.c store_test)
add_mini_redis_test(test_resp tests/test_resp.c resp_test)
add_mini_redis_test(test_command tests/test_command.c command_test)
add_mini_redis_test(test_request_processor tests/test_request_processor.c request_processor_test)

if(UNIX)
    add_mini_redis_test(test_server tests/test_server.c server_test)
endif()
