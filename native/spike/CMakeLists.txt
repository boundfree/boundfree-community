cmake_minimum_required(VERSION 3.21)

project(bf_spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

add_executable(bf_spike
    main.cpp
    waveform_gl.cpp
    waveform_gl.h
)
target_link_libraries(bf_spike PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
