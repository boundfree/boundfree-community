cmake_minimum_required(VERSION 3.21)

project(boundfree_eeg_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# License configuration.
# BF_REQUIRE_LICENSE=OFF (default): dev builds run without a configured public
#   key; LicenseManager reports NotConfigured but the app still compiles/runs.
# BF_REQUIRE_LICENSE=ON: release builds must embed a public key. Requires
#   BF_LICENSE_PUBLIC_KEY_FILE pointing to a SubjectPublicKeyInfo PEM. CMake
#   generates a raw-string header from the PEM at configure time and defines
#   BF_LICENSE_PUBLIC_KEY_HEADER. Private keys are never referenced here.
# ---------------------------------------------------------------------------
option(BF_REQUIRE_LICENSE "Require an embedded release public key for license verification" OFF)
set(BF_LICENSE_PUBLIC_KEY_FILE "" CACHE FILEPATH "Path to RSA-2048 public key PEM (SubjectPublicKeyInfo)")

set(BF_DSP_SOURCES
    src/dsp/biquad.cpp
    src/dsp/filter_design.cpp
    src/dsp/npy_io.cpp
    src/dsp/pipeline.cpp
    src/dsp/reference.cpp
    src/dsp/spectral.cpp
    src/dsp/spike_detect.cpp
    src/dsp/spike_sort.cpp
)

add_library(bf_dsp STATIC ${BF_DSP_SOURCES})
target_include_directories(bf_dsp PUBLIC src third_party)

# ---------------------------------------------------------------------------
# bf_core: shared base (controller/ipc/license/render/i18n/core + shared UI
# widgets) reused by both the EEG executable (bf_app) and the Spike executable
# (bf_spike). EEG-specific sources (main window, spectral analysis widgets,
# analysis_service) stay in bf_app; Spike-specific sources live in bf_spike.
# This keeps the working EEG product unchanged while enabling a second product
# line to reuse the same base without a rewrite.
# ---------------------------------------------------------------------------
set(BF_CORE_SOURCES
    src/controller/export_controller.cpp
    src/controller/export_controller.h
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
    src/controller/record_controller.cpp
    src/controller/record_controller.h
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
    src/license/license_crypto.cpp
    src/license/license_crypto.h
    src/license/license_key.cpp
    src/license/license_key.h
    src/license/license_manager.cpp
    src/license/license_manager.h
    src/render/minmax_decimator.cpp
    src/render/minmax_decimator.h
    src/render/waveform_slice.cpp
    src/render/waveform_slice.h
    src/ui/waveform_widget.cpp
    src/ui/waveform_widget.h
    src/ui/video_panel.cpp
    src/ui/video_panel.h
    src/ui/collapsible_panel.cpp
    src/ui/collapsible_panel.h
)

add_library(bf_core STATIC ${BF_CORE_SOURCES})
target_include_directories(bf_core PUBLIC src)
target_compile_definitions(bf_core PUBLIC BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_core PUBLIC Qt6::Core Qt6::Widgets Qt6::Multimedia bf_dsp)
if(WIN32)
    target_link_libraries(bf_core PUBLIC bcrypt iphlpapi)
endif()

# ---------------------------------------------------------------------------
# bf_app: EEG analysis executable (v1 product). EEG-specific sources only;
# everything shared comes from bf_core. Name and behavior are unchanged so the
# existing test suite and M8 packaging pipeline keep working.
# ---------------------------------------------------------------------------
add_executable(bf_app
    src/app/main.cpp
    src/controller/analysis_service.cpp
    src/controller/analysis_service.h
    src/ui/eeg_processing_panel.cpp
    src/ui/eeg_processing_panel.h
    src/ui/eeg_playback_panel.cpp
    src/ui/eeg_playback_panel.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/analysis_widgets.cpp
    src/ui/analysis_widgets.h
)
target_include_directories(bf_app PRIVATE src)
target_link_libraries(bf_app PRIVATE bf_core)

# ---------------------------------------------------------------------------
# bf_spike: Spike analysis executable (next-generation product line). Reuses
# bf_core for base UI/IPC/license/i18n/render and bf_dsp for spike detection
# and sorting. Spike-specific window, widgets and analysis service live here.
# ---------------------------------------------------------------------------
set(BF_SPIKE_SOURCES
    src/app/spike_main.cpp
    src/controller/spike_analysis_service.cpp
    src/controller/spike_analysis_service.h
    src/ui/spike_widgets.cpp
    src/ui/spike_widgets.h
    src/ui/spike_window.cpp
    src/ui/spike_window.h
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/spike_main.cpp")
    add_executable(bf_spike ${BF_SPIKE_SOURCES})
    target_include_directories(bf_spike PRIVATE src)
    target_link_libraries(bf_spike PRIVATE bf_core)
endif()

add_executable(bf_license_smoke
    src/license/license_smoke.cpp
    src/license/app_error_stub.cpp
    src/core/app_error.h
    src/license/machine_id.cpp
    src/license/machine_id.h
    src/license/license_crypto.cpp
    src/license/license_crypto.h
    src/license/license_key.cpp
    src/license/license_key.h
    src/license/license_manager.cpp
    src/license/license_manager.h
)
target_include_directories(bf_license_smoke PRIVATE src)
target_compile_definitions(bf_license_smoke PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_license_smoke PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(bf_license_smoke PRIVATE bcrypt iphlpapi)
endif()

# Generate the embedded public key header when BF_REQUIRE_LICENSE=ON.
# Must run after bf_app and bf_license_smoke targets exist so target_* calls
# resolve. The header contains SubjectPublicKeyInfo PEM only (no private key).
if(BF_REQUIRE_LICENSE)
    if(NOT BF_LICENSE_PUBLIC_KEY_FILE OR NOT EXISTS "${BF_LICENSE_PUBLIC_KEY_FILE}")
        message(FATAL_ERROR
            "BF_REQUIRE_LICENSE=ON requires BF_LICENSE_PUBLIC_KEY_FILE to point to an existing PEM public key. "
            "Private keys must never be committed; only the SubjectPublicKeyInfo PEM.")
    endif()
    file(READ "${BF_LICENSE_PUBLIC_KEY_FILE}" _bf_license_pem)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bf_license_public_key_embed.h"
"#pragma once
// Generated by CMake from BF_LICENSE_PUBLIC_KEY_FILE. SubjectPublicKeyInfo PEM only.
// BF_REQUIRE_LICENSE=ON: release public key for offline license verification.
// This header MUST NOT be committed; it contains no private key material.
namespace bf { namespace license_detail {
inline const char* const kPublicKeyPem = R\"BFPEM(
${_bf_license_pem}
)BFPEM\";
}}  // namespace bf::license_detail
")
    # BF_REQUIRE_LICENSE=1 lets main.cpp enforce startup license check.
    target_compile_definitions(bf_app PRIVATE BF_REQUIRE_LICENSE=1 BF_LICENSE_PUBLIC_KEY_HEADER)
    target_include_directories(bf_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(bf_license_smoke PRIVATE BF_LICENSE_PUBLIC_KEY_HEADER)
    target_include_directories(bf_license_smoke PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

add_executable(m3_dsp_test src/dsp/m3_dsp_test.cpp)
target_include_directories(m3_dsp_test PRIVATE src)
target_link_libraries(m3_dsp_test PRIVATE bf_dsp)

add_executable(m4_spectral_test src/dsp/m4_spectral_test.cpp)
target_include_directories(m4_spectral_test PRIVATE src)
target_link_libraries(m4_spectral_test PRIVATE bf_dsp Threads::Threads)

add_executable(spike_dsp_test src/dsp/spike_detect.cpp src/dsp/spike_sort.cpp src/dsp/spike_dsp_test.cpp)
target_include_directories(spike_dsp_test PRIVATE src third_party)

add_executable(collapsible_panel_test src/ui/collapsible_panel_test.cpp)
target_include_directories(collapsible_panel_test PRIVATE src)
target_link_libraries(collapsible_panel_test PRIVATE bf_core)

add_executable(i18n_locale_format_test src/i18n/i18n_locale_format_test.cpp)
target_include_directories(i18n_locale_format_test PRIVATE src)
target_compile_definitions(i18n_locale_format_test PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(i18n_locale_format_test PRIVATE bf_core)

add_executable(processing_view_state_test src/controller/processing_view_state_test.cpp)
target_include_directories(processing_view_state_test PRIVATE src)
target_link_libraries(processing_view_state_test PRIVATE bf_core Qt6::Core)

add_executable(spike_golden_test src/dsp/spike_detect.cpp src/dsp/spike_sort.cpp src/dsp/npy_io.cpp src/dsp/spike_golden_test.cpp)
target_include_directories(spike_golden_test PRIVATE src third_party)

# ---------------------------------------------------------------------------
# Install rules: stage bf_app + source-tree assets into a release tree.
# Assets are referenced relative to the install source dir (no hardcoded
# host paths); at runtime bf_app resolves version.json/contracts/i18n from
# BF_RESOURCE_ROOT, the app dir, or the compiled repo root.
# ---------------------------------------------------------------------------

install(TARGETS bf_app RUNTIME DESTINATION .)

# version.json at stage root (runtime resolves it from BF_RESOURCE_ROOT or
# the app directory). error_codes.json goes under contracts/ to match the
# sidecar's repo_root() / "contracts" / "error_codes.json" lookup.
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../version.json
    DESTINATION .
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../contracts/error_codes.json
    DESTINATION contracts
)

# i18n .ts sources (the runtime reads .ts directly; no lrelease .qm step).
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/i18n/
    DESTINATION i18n
    FILES_MATCHING PATTERN "*.ts"
)

# Legal assets.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../legal/
    DESTINATION legal
    FILES_MATCHING
        PATTERN "*.txt"
        PATTERN "*.md"
        PATTERN "licenses/*"
)
