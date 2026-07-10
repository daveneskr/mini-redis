cmake_minimum_required(VERSION 3.26)

project(mini_redis C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(mini_redis
        src/main.c
)