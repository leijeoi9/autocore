cmake_minimum_required(VERSION 3.16)

project(AutoCore C)

add_subdirectory(service/logger)
add_subdirectory(rte)
add_subdirectory(service/can_driver)
add_subdirectory(app/can_service)