cmake_minimum_required(VERSION 3.28)
project(order_matching_engine)

set(CMAKE_CXX_STANDARD 23)

add_executable(order_matching_engine
        src/main.cpp
        src/order/order.cpp
        src/order/order.h
)
