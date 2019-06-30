cmake_minimum_required(VERSION 3.10)
project(dptrp1-test)

add_executable(${PROJECT_NAME} test.cc)

target_link_libraries(
    ${PROJECT_NAME} 
    PUBLIC
        dptrp1
)
