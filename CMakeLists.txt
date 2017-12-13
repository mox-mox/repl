cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

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

set(REPL_INCLUDE_DIRS
	${CMAKE_CURRENT_SOURCE_DIR}
)

set(REPL_LIBRARY_DIRS
	${PROJECT_BINARY_DIR}
)

#file(GLOB_RECURSE SRC *.cpp)
set(SRC
	repl.cpp
)
add_library(repl ${SRC})

target_include_directories(repl
	PUBLIC
		${REPL_INCLUDE_DIRS}
)

link_directories(repl
	PUBLIC
		${REPL_LIBRARY_DIRS}
)
target_link_libraries(repl
	PUBLIC
	${GETOPT_LIBRARIES}
)

target_compile_options(repl
	PRIVATE -std=c++17 -Wall -Wextra -Wpedantic -Werror -Wfatal-errors
)

if (CMAKE_BUILD_TYPE STREQUAL "DEBUG")
	target_compile_options(repl
		PUBLIC -g3
	)
elseif (CMAKE_BUILD_TYPE EQUAL "RELEASE")
	target_compile_options(repl
		PUBLIC -O2
	)
endif()


set(REPL_LIBRARY_DIRS "${REPL_LIBRARY_DIRS} ${PROJECT_BINARY_DIR}")
set(REPL_LIBRARIES "${REPL_LIBRARIES};repl")

get_directory_property(HAS_PARENT_DIRECTORY PARENT_DIRECTORY)
if(HAS_PARENT_DIRECTORY)
	set(REPL_LIBRARY_DIRS ${REPL_LIBRARY_DIRS} PARENT_SCOPE)
	set(REPL_INCLUDE_DIRS ${REPL_INCLUDE_DIRS} PARENT_SCOPE)
	set(REPL_LIBRARIES ${REPL_LIBRARIES} PARENT_SCOPE)
endif()
