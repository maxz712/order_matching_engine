cmake_minimum_required(VERSION 3.10)
project(MatchingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -pthread")

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)

# For tests
enable_testing()
find_package(GTest REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
        src/matching_engine.cpp
        src/server.cpp
        src/main.cpp
)

add_executable(order_matching_engine ${SOURCES})
target_link_libraries(order_matching_engine
        ${Boost_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Tests
set(TEST_SOURCES
        tests/test_matching.cpp
        src/matching_engine.cpp
)
add_executable(test_engine ${TEST_SOURCES})
target_link_libraries(test_engine
        ${Boost_LIBRARIES}
        GTest::GTest
        GTest::Main
)
add_test(NAME matching_tests COMMAND test_engine)
