cmake_minimum_required(VERSION 3.21)

project(boundfree_eeg_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia)
find_package(Threads REQUIRED)

set(BF_DSP_SOURCES
    src/dsp/biquad.cpp
    src/dsp/filter_design.cpp
    src/dsp/npy_io.cpp
    src/dsp/pipeline.cpp
    src/dsp/reference.cpp
    src/dsp/spectral.cpp
)

add_library(bf_dsp STATIC ${BF_DSP_SOURCES})
target_include_directories(bf_dsp PUBLIC src third_party)

add_executable(bf_app
    src/controller/analysis_service.cpp
    src/controller/analysis_service.h
    src/app/main.cpp
    src/controller/file_open_controller.cpp
    src/controller/file_open_controller.h
    src/controller/file_session.cpp
    src/controller/file_session.h
    src/controller/processing_view_state.cpp
    src/controller/processing_view_state.h
    src/controller/request_generation.cpp
    src/controller/request_generation.h
    src/controller/marker_store.cpp
    src/controller/marker_store.h
    src/controller/playback_state.cpp
    src/controller/playback_state.h
    src/controller/video_sync_state.cpp
    src/controller/video_sync_state.h
    src/controller/waveform_view_state.cpp
    src/controller/waveform_view_state.h
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
    src/ui/analysis_widgets.cpp
    src/ui/analysis_widgets.h
    src/ui/video_panel.cpp
    src/ui/video_panel.h
)

target_include_directories(bf_app PRIVATE src)
target_compile_definitions(bf_app PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_app PRIVATE Qt6::Core Qt6::Widgets Qt6::Multimedia bf_dsp)

add_executable(m3_dsp_test src/dsp/m3_dsp_test.cpp)
target_include_directories(m3_dsp_test PRIVATE src)
target_link_libraries(m3_dsp_test PRIVATE bf_dsp)

add_executable(m4_spectral_test src/dsp/m4_spectral_test.cpp)
target_include_directories(m4_spectral_test PRIVATE src)
target_link_libraries(m4_spectral_test PRIVATE bf_dsp Threads::Threads)
