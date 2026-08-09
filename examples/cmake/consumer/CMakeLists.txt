cmake_minimum_required(VERSION 3.16)

project(openaliro_consumer LANGUAGES C)

find_package(OpenAliro CONFIG REQUIRED)

add_executable(openaliro_consumer main.c hal_contract.c)
target_link_libraries(openaliro_consumer PRIVATE OpenAliro::tlv)
