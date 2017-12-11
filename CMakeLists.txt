cmake_minimum_required(VERSION 3.2)

#
# Building in-tree is not allowed (we take care of your craziness).
#

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there. Thank you.")
endif()

#
# Project configuration
#

project(repl)


file(GLOB SRCS_LOCAL *.cpp)
set(SRCS ${SRCS} ${SRCS_LOCAL})
add_executable(example ${SRCS})
target_compile_options(example PUBLIC -std=c++17 -Wall -Wextra -Wpedantic -Werror -Wfatal-errors)
