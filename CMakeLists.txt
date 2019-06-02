cmake_minimum_required(VERSION 3.10)
project(dptrp1)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(
    ${PROJECT_NAME} STATIC
    src/dptrp1.cc
    src/dtree.cc
    src/revdb.cc
    src/logger.cc
    include/dptrp1.h
    include/dtree.h
    include/revdb.h
    include/logger.h
)

file(COPY templates/rev_db DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY templates/gitignore DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        include
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
)

target_link_directories(
    ${PROJECT_NAME}
    PUBLIC
)

target_compile_features(
    ${PROJECT_NAME} PUBLIC cxx_std_11
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
set(DEBUG 1)
endif()

if(DEBUG)
set(CMAKE_CXX_FLAGS "-g -Wall -DBOOST_LOG_DYN_LINK")
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        DEBUG
        DEBUG_AUTH
        DEBUG_CONFLICT
        DEBUG_GIT
        # DEBUG_REQUEST
        # DEBUG_DEBUG_FILE_IO
)
endif()

if(WIN32)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        deps/boost/win/debug/include
)
target_link_directories(
    ${PROJECT_NAME}
    PUBLIC
        deps/boost/win/debug/lib
)
endif(WIN32)

if(APPLE)
find_package(Boost COMPONENTS filesystem REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
MESSAGE("Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
MESSAGE("Boost_LIBRARIES = ${Boost_LIBRARIES}")
MESSAGE("OPENSSL_INCLUDE_DIR = ${OPENSSL_INCLUDE_DIR}")
MESSAGE("OPENSSL_LIBRARIES = ${OPENSSL_LIBRARIES}")
MESSAGE("SQLite3_INCLUDE_DIRS = ${SQLite3_INCLUDE_DIRS}")
MESSAGE("SQLite3_LIBRARIES = ${SQLite3_LIBRARIES}")
set_target_properties(
    ${PROJECT_NAME} 
    PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER com.dptair.dptrp1
        MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist
        PUBLIC_HEADER 
            "include/dptrp1.h;include/dtree.h;include/revdb.h;deps/NFHTTP/mac/include/NFHTTP"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Yu Li"
)
target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        ${Boost_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${SQLite3_INCLUDE_DIRS}
        deps/NFHTTP/mac/include
)
target_link_libraries(
    ${PROJECT_NAME} 
    PUBLIC
        "-framework Foundation"
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${SQLite3_LIBRARIES}
        NFHTTP
)
target_link_directories(
    ${PROJECT_NAME}
    PUBLIC
    deps/NFHTTP/mac/lib
)
endif(APPLE)

add_subdirectory(examples)
