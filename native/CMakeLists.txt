cmake_minimum_required(VERSION 3.21)

project(boundfree_eeg_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_executable(bf_app
    src/app/main.cpp
    src/controller/file_open_controller.cpp
    src/controller/file_open_controller.h
    src/controller/file_session.cpp
    src/controller/file_session.h
    src/controller/request_generation.cpp
    src/controller/request_generation.h
    src/core/app_error.cpp
    src/core/app_error.h
    src/core/version.cpp
    src/core/version.h
    src/i18n/i18n_service.cpp
    src/i18n/i18n_service.h
    src/ipc/sidecar_client.cpp
    src/ipc/sidecar_client.h
    src/license/machine_id.cpp
    src/license/machine_id.h
    src/render/minmax_decimator.cpp
    src/render/minmax_decimator.h
    src/render/waveform_slice.cpp
    src/render/waveform_slice.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/waveform_widget.cpp
    src/ui/waveform_widget.h
)

target_include_directories(bf_app PRIVATE src)
target_compile_definitions(bf_app PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_app PRIVATE Qt6::Core Qt6::Widgets)
