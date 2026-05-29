cmake_minimum_required(VERSION 3.16)
project(gui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml SerialPort Widgets)  # <-- Widgets added

qt_add_executable(gui main.cpp)
qt_add_qml_module(gui
    URI app
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(gui PRIVATE Qt6::Quick Qt6::Qml Qt6::SerialPort Qt6::Widgets)
