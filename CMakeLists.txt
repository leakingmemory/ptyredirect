cmake_minimum_required(VERSION 3.30)
project(ptyredirect)

set(CMAKE_CXX_STANDARD 23)

add_executable(ptyredirect main.cpp)

install(TARGETS ptyredirect DESTINATION bin)
