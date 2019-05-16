cmake_minimum_required(VERSION 3.10)

add_executable(example main.cc)

target_link_libraries(
    example
    PUBLIC
        dptrp1
)

