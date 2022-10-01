cmake_minimum_required(VERSION 3.10)

# set the project name
project(server)

# add the executable
add_executable(server src/main.cpp)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# require boost library
cmake_policy(SET CMP0074 NEW)
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
set(Boost_USE_STATIC_LIBS ON)

# add the websocketpp library
add_subdirectory(websocketpp)

target_include_directories(
	server PUBLIC "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/websocketpp"
)