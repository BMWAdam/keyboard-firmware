cmake_minimum_required(VERSION 3.16)
project(gui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# The executable target must match the 'cp' commands in the flake ("gui")
add_executable(gui main.cpp)
target_link_libraries(gui PRIVATE Qt6::Widgets)
