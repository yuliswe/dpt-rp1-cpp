cmake_minimum_required(VERSION 3.10)
project(dptrp1)

set(CMAKE_BUILD_TYPE RelwithDebInfo)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(
    ${PROJECT_NAME} STATIC
    src/dptrp1.cc
    src/dtree.cc
    src/revdb.cc
    src/git.cc
    include/dptrp1/dptrp1.h
    include/dptrp1/dtree.h
    include/dptrp1/revdb.h
    include/dptrp1/git.h
)

file(COPY templates/rev_db DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY templates/gitignore DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# copy headers
file(COPY include DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY deps/NFHTTP/mac/include/NFHTTP DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/include)

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
    ${PROJECT_NAME} PUBLIC cxx_std_17
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
set(DEBUG 1)
endif()

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        DEBUG_AUTH
        DEBUG_CONFLICT
        # DEBUG_GIT
        # DEBUG_REQUEST
        DEBUG_FILE_IO
)

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
# find_package(ZLIB REQUIRED)
# find_package(Iconv REQUIRED)
MESSAGE("Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
MESSAGE("Boost_LIBRARIES = ${Boost_LIBRARIES}")
MESSAGE("OPENSSL_INCLUDE_DIR = ${OPENSSL_INCLUDE_DIR}")
MESSAGE("OPENSSL_LIBRARIES = ${OPENSSL_LIBRARIES}")
MESSAGE("SQLite3_INCLUDE_DIRS = ${SQLite3_INCLUDE_DIRS}")
MESSAGE("SQLite3_LIBRARIES = ${SQLite3_LIBRARIES}")
MESSAGE("ZLIB_INCLUDE_DIRS = ${ZLIB_INCLUDE_DIRS}")
MESSAGE("ZLIB_LIBRARIES = ${ZLIB_LIBRARIES}")
MESSAGE("Iconv_INCLUDE_DIRS = ${Iconv_INCLUDE_DIRS}")
MESSAGE("Iconv_LIBRARIES = ${Iconv_LIBRARIES}")

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        ${Boost_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${SQLite3_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${Iconv_INCLUDE_DIRS}
        deps/NFHTTP/mac/include
        deps/libgit2/mac/include
)
target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
        "-framework Foundation"
        "-framework Security"
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${SQLite3_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${Iconv_LIBRARIES}
        NFHTTP
        git2
)
target_link_directories(
    ${PROJECT_NAME}
    PUBLIC
        deps/NFHTTP/mac/lib
        deps/libgit2/mac/lib
)
endif(APPLE)

install(TARGETS ${PROJECT_NAME})
install(DIRECTORY include DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY deps/NFHTTP/mac/include DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY deps/libgit2/mac/include DESTINATION ${CMAKE_INSTALL_PREFIX})

add_subdirectory(examples)
add_subdirectory(tests)
