cmake_minimum_required(VERSION 3.20)
project(cxpr_consumer LANGUAGES C)

find_package(cxpr CONFIG REQUIRED)

add_executable(cxpr_consumer main.c)
target_link_libraries(cxpr_consumer PRIVATE cxpr::cxpr)
