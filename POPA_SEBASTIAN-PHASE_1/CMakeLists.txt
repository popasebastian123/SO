cmake_minimum_required(VERSION 3.20)
project(city_manager C)

set(CMAKE_C_STANDARD 11)

add_executable(city_manager city_manager.c)
add_executable(monitor_reports monitor_reports.c)