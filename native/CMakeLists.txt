cmake_minimum_required(VERSION 3.21)

project(boundfree_eeg_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(BF_BRANDING_QRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding.qrc")
set(BF_WINDOWS_APP_ICON_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/boundfree_app.rc")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia Network LinguistTools)
find_package(Threads REQUIRED)
if(NOT WIN32)
    # RSA-2048 PSS verification backend on Linux/WSL2 (license_crypto.cpp).
    # Windows uses the CNG backend and needs no OpenSSL.
    find_package(OpenSSL REQUIRED)
endif()

# ---------------------------------------------------------------------------
# 翻译：用 Qt6 LinguistTools/lrelease 把 native/i18n/*.ts 编译为 .qm。
# 仅 stable + test locale（zh_CN/en_US/ja_JP/zh_TW/pseudo）产出 .qm；其余 locale 保留 .ts 源但不产出 .qm、不进包。
# bf_app 构建前生成；install 只安装 .qm，不安装 .ts。Qt 基础翻译按可用性部署。
# ---------------------------------------------------------------------------
set(BF_I18N_TS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
set(BF_I18N_QM_DIR "${CMAKE_CURRENT_BINARY_DIR}/i18n")
set(BF_I18N_LOCALES zh_CN en_US ja_JP zh_TW pseudo)
set(BF_I18N_TS_FILES)
foreach(_loc ${BF_I18N_LOCALES})
    set(_ts_file "${BF_I18N_TS_DIR}/bf_app_${_loc}.ts")
    list(APPEND BF_I18N_TS_FILES "${_ts_file}")
    # Qt 6.8 的 qt_add_lrelease 不可靠地处理 QM_OUTPUT_DIRECTORY；
    # TS source property 是 Qt 6.8/6.10 都支持的输出目录契约。
    set_source_files_properties("${_ts_file}" PROPERTIES OUTPUT_LOCATION "${BF_I18N_QM_DIR}")
endforeach()
# qt_add_lrelease 创建 lrelease 目标并把 .qm 写入 BF_I18N_QM_DIR。
# 仅 stable + pseudo 产出 .qm；其余 locale 保留 .ts 源但不产出 .qm、不进包。
qt_add_lrelease(
    LRELEASE_TARGET bf_i18n_qm
    QM_OUTPUT_DIRECTORY "${BF_I18N_QM_DIR}"
    QM_FILES_OUTPUT_VARIABLE BF_I18N_QM_FILES
    TS_FILES ${BF_I18N_TS_FILES}
)

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
    src/license/clock_rollback.cpp
    src/license/clock_rollback.h
    src/license/machine_id.cpp
    src/license/machine_id.h
    src/license/license_crypto.cpp
    src/license/license_crypto.h
    src/license/license_key.cpp
    src/license/license_key.h
    src/license/license_manager.cpp
    src/license/license_manager.h
    src/license/online_activation_client.cpp
    src/license/online_activation_client.h
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
target_link_libraries(bf_core PUBLIC Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::Network bf_dsp)
if(NOT WIN32)
    target_link_libraries(bf_core PUBLIC OpenSSL::Crypto)
endif()
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
    src/ui/record_metadata_dialog.cpp
    src/ui/record_metadata_dialog.h
    src/ui/activation_dialog.cpp
    src/ui/activation_dialog.h
    src/ui/analysis_widgets.cpp
    src/ui/analysis_widgets.h
)
target_include_directories(bf_app PRIVATE src)
target_link_libraries(bf_app PRIVATE bf_core Qt6::Network)
target_sources(bf_app PRIVATE ${BF_BRANDING_QRC})
if(WIN32)
    target_sources(bf_app PRIVATE ${BF_WINDOWS_APP_ICON_RC})
endif()
add_dependencies(bf_app bf_i18n_qm)
# Visual Studio 等多配置生成器把 exe 放在 <build>/<config>/；构建后把
# 开发/测试所需 QM 邻接到 exe。install 仍只安装 stable QM。
if(CMAKE_CONFIGURATION_TYPES)
    add_custom_command(TARGET bf_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:bf_app>/i18n"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${BF_I18N_QM_FILES} "$<TARGET_FILE_DIR:bf_app>/i18n"
        VERBATIM
    )
endif()

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
    target_sources(bf_spike PRIVATE ${BF_BRANDING_QRC})
    if(WIN32)
        target_sources(bf_spike PRIVATE ${BF_WINDOWS_APP_ICON_RC})
    endif()
    add_dependencies(bf_spike bf_i18n_qm)
    if(CMAKE_CONFIGURATION_TYPES)
        add_custom_command(TARGET bf_spike POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:bf_spike>/i18n"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${BF_I18N_QM_FILES} "$<TARGET_FILE_DIR:bf_spike>/i18n"
            VERBATIM
        )
    endif()
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
    src/license/clock_rollback.cpp
    src/license/clock_rollback.h
)
target_include_directories(bf_license_smoke PRIVATE src)
target_compile_definitions(bf_license_smoke PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_license_smoke PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(bf_license_smoke PRIVATE bcrypt iphlpapi)
else()
    target_link_libraries(bf_license_smoke PRIVATE OpenSSL::Crypto)
endif()

# Generate the embedded public key header when BF_REQUIRE_LICENSE=ON.
# Must run after bf_core / bf_app / bf_license_smoke targets exist.
# license_manager.cpp lives in bf_core, so the embed macro and include path
# MUST be applied to bf_core (not only bf_app). Otherwise the static library
# is compiled with an empty embeddedPublicKey() and runtime reports
# reason=not_configured even though bf_app has BF_REQUIRE_LICENSE=1.
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
    # bf_core owns LicenseManager; embed the public key into that translation unit.
    target_compile_definitions(bf_core PUBLIC BF_LICENSE_PUBLIC_KEY_HEADER)
    target_include_directories(bf_core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    # bf_app startup gate (main.cpp ensureOfflineLicense).
    target_compile_definitions(bf_app PRIVATE BF_REQUIRE_LICENSE=1)
    # Standalone license smoke also verifies the embed path.
    target_compile_definitions(bf_license_smoke PRIVATE BF_LICENSE_PUBLIC_KEY_HEADER)
    target_include_directories(bf_license_smoke PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

add_executable(m3_dsp_test src/dsp/m3_dsp_test.cpp)
target_include_directories(m3_dsp_test PRIVATE src)
target_link_libraries(m3_dsp_test PRIVATE bf_dsp)

add_executable(m4_spectral_test src/dsp/m4_spectral_test.cpp)
target_include_directories(m4_spectral_test PRIVATE src)
target_link_libraries(m4_spectral_test PRIVATE bf_dsp Threads::Threads)

add_executable(spectrum_render_test src/ui/spectrum_render_test.cpp src/ui/analysis_widgets.cpp)
target_include_directories(spectrum_render_test PRIVATE src)
target_link_libraries(spectrum_render_test PRIVATE bf_dsp Qt6::Widgets)

add_executable(spike_dsp_test src/dsp/spike_detect.cpp src/dsp/spike_sort.cpp src/dsp/spike_dsp_test.cpp)
target_include_directories(spike_dsp_test PRIVATE src third_party)

add_executable(collapsible_panel_test src/ui/collapsible_panel_test.cpp)
target_include_directories(collapsible_panel_test PRIVATE src)
target_link_libraries(collapsible_panel_test PRIVATE bf_core)

# 共享绘图口径（边距/配色/刻度算法）是纯头文件，只依赖 Qt，不链接 bf_core。
add_executable(plot_style_test src/ui/plot_style_test.cpp)
target_include_directories(plot_style_test PRIVATE src)
target_link_libraries(plot_style_test PRIVATE Qt6::Widgets)

add_executable(i18n_locale_format_test src/i18n/i18n_locale_format_test.cpp)
target_include_directories(i18n_locale_format_test PRIVATE src)
target_compile_definitions(i18n_locale_format_test PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(i18n_locale_format_test PRIVATE bf_core)
add_dependencies(i18n_locale_format_test bf_i18n_qm)

add_executable(processing_view_state_test src/controller/processing_view_state_test.cpp)
target_include_directories(processing_view_state_test PRIVATE src)
target_link_libraries(processing_view_state_test PRIVATE bf_core Qt6::Core)

add_executable(online_activation_client_test src/license/online_activation_client_test.cpp)
target_include_directories(online_activation_client_test PRIVATE src)
target_link_libraries(online_activation_client_test PRIVATE bf_core Qt6::Network)
add_executable(clock_rollback_test src/license/clock_rollback_test.cpp)
target_include_directories(clock_rollback_test PRIVATE src)
target_link_libraries(clock_rollback_test PRIVATE bf_core Qt6::Core)
# LicenseManager clock-watermark end-to-end test. Unix-only: generates a
# throwaway RSA-2048 key pair in memory via OpenSSL (no Windows CNG
# private-key signing path) and redirects XDG_DATA_HOME into a QTemporaryDir.
if(NOT WIN32)
    add_executable(license_manager_clock_test src/license/license_manager_clock_test.cpp)
    target_include_directories(license_manager_clock_test PRIVATE src)
    target_link_libraries(license_manager_clock_test PRIVATE bf_core Qt6::Core OpenSSL::Crypto)
endif()
add_executable(activation_dialog_test
    src/ui/activation_dialog_test.cpp
    src/ui/activation_dialog.cpp
)
target_include_directories(activation_dialog_test PRIVATE src)
target_compile_definitions(activation_dialog_test PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(activation_dialog_test PRIVATE bf_core Qt6::Widgets Qt6::Network)
add_dependencies(activation_dialog_test bf_i18n_qm)

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

# i18n .qm：发布产物只安装已验收的 stable locale；pseudo 仅留在构建目录供测试。
# 运行时从 BF_RESOURCE_ROOT/应用目录/repo root 下的 i18n/bf_app_<locale>.qm 加载。
install(FILES
    "${BF_I18N_QM_DIR}/bf_app_zh_CN.qm"
    "${BF_I18N_QM_DIR}/bf_app_en_US.qm"
    "${BF_I18N_QM_DIR}/bf_app_ja_JP.qm"
    "${BF_I18N_QM_DIR}/bf_app_zh_TW.qm"
    DESTINATION i18n
)

# Legal assets.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../legal/
    DESTINATION legal
    FILES_MATCHING
        PATTERN "*.txt"
        PATTERN "*.md"
        PATTERN "licenses/*"
)

add_executable(waveform_viewport_test src/ui/waveform_viewport_test.cpp)
target_link_libraries(waveform_viewport_test PRIVATE bf_core)

add_executable(waveform_channels_test src/ui/waveform_channels_test.cpp)
target_link_libraries(waveform_channels_test PRIVATE bf_core)

add_executable(record_metadata_dialog_test src/ui/record_metadata_dialog_test.cpp src/ui/record_metadata_dialog.cpp)
target_link_libraries(record_metadata_dialog_test PRIVATE bf_core Qt6::Widgets)
target_compile_definitions(record_metadata_dialog_test PRIVATE BF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_dependencies(record_metadata_dialog_test bf_i18n_qm)

add_executable(analysis_widgets_render_test src/ui/analysis_widgets_render_test.cpp src/ui/analysis_widgets.cpp)
target_include_directories(analysis_widgets_render_test PRIVATE src)
target_link_libraries(analysis_widgets_render_test PRIVATE bf_dsp Qt6::Widgets)
