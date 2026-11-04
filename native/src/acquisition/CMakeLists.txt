# Keep device acquisition independent of offline DSP, licensing and widgets.
add_library(bf_signal STATIC model/signal_model.cpp model/clock_mapping.cpp)
target_include_directories(bf_signal PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_signal PUBLIC Threads::Threads)
set_target_properties(bf_signal PROPERTIES AUTOMOC OFF AUTORCC OFF)

add_library(bf_acquisition STATIC
    adapters/simulator.cpp
    adapters/rt_bci_decoder.cpp
    buffer/signal_hub.cpp
    session/acquisition_session.cpp
)
target_link_libraries(bf_acquisition PUBLIC bf_signal)
set_target_properties(bf_acquisition PROPERTIES AUTOMOC OFF AUTORCC OFF)

add_library(bf_protocol STATIC
    protocol/codec.cpp
    protocol/json.cpp
    protocol/json_syntax.cpp
    protocol/stream_parser.cpp
)
target_include_directories(bf_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_protocol PUBLIC Qt6::Core)

add_library(bf_transport STATIC transport/bfsp_tcp_client.cpp transport/bfsp_tcp_client.h)
target_link_libraries(bf_transport PUBLIC bf_protocol Qt6::Network)

option(BFSP_ENABLE_SERIALPORT "Enable BFSP serial transport when Qt SerialPort is installed" ON)
if(BFSP_ENABLE_SERIALPORT)
    find_package(Qt6SerialPort QUIET)
    if(TARGET Qt6::SerialPort)
        target_link_libraries(bf_transport PUBLIC Qt6::SerialPort)
        target_compile_definitions(bf_transport PUBLIC BFSP_ENABLE_QT_SERIALPORT)
    endif()
endif()

function(bf_acquisition_test target source library)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${library})
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

bf_acquisition_test(signal_model_test model/signal_model_test.cpp bf_acquisition)
bf_acquisition_test(clock_mapping_test model/clock_mapping_test.cpp bf_signal)
bf_acquisition_test(acquisition_simulator_test adapters/acquisition_simulator_test.cpp bf_acquisition)
bf_acquisition_test(rt_bci_decoder_test adapters/rt_bci_decoder_test.cpp bf_acquisition)
bf_acquisition_test(bfsp_protocol_test protocol/bfsp_protocol_test.cpp bf_protocol)
bf_acquisition_test(bfsp_tcp_client_test transport/bfsp_tcp_client_test.cpp bf_transport)

add_library(bf_acquisition_format STATIC ../controller/acquisition_format.cpp)
target_include_directories(bf_acquisition_format PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(bf_acquisition_format PUBLIC bf_signal bf_protocol)
bf_acquisition_test(acquisition_format_test ../controller/acquisition_format_test.cpp bf_acquisition_format)

add_library(bf_device_ui STATIC
    ../controller/live_ica_controller.cpp
    ../controller/live_ica_controller.h
    ../controller/acquisition_diagnostic_log.cpp
    ../controller/acquisition_diagnostic_log.h
    ../controller/acquisition_controller.cpp
    ../controller/acquisition_controller.h
    ../controller/acquisition_state.h
    ../controller/acquisition_source.h
    ../controller/acquisition_wifi_source.cpp
    ../controller/acquisition_wifi_source.h
    ../controller/acquisition_tcp_source.cpp
    ../controller/acquisition_tcp_source.h
    ../controller/acquisition_view.cpp
    ../controller/acquisition_view.h
    ../controller/capture_recorder.cpp
    ../controller/capture_recorder.h
    ../ui/device_workspace.cpp
    ../ui/device_workspace.h
)
target_link_libraries(bf_device_ui PUBLIC bf_core bf_acquisition bf_acquisition_format bf_transport)

bf_acquisition_test(acquisition_ui_test ../controller/acquisition_ui_test.cpp bf_device_ui)
bf_acquisition_test(acquisition_diagnostic_log_test ../controller/acquisition_diagnostic_log_test.cpp bf_device_ui)
bf_acquisition_test(acquisition_waveform_test ../controller/acquisition_waveform_test.cpp bf_device_ui)
add_dependencies(acquisition_ui_test bf_i18n_qm)
bf_acquisition_test(acquisition_tcp_source_test ../controller/acquisition_tcp_source_test.cpp bf_device_ui)
bf_acquisition_test(acquisition_wifi_source_test ../controller/acquisition_wifi_source_test.cpp bf_device_ui)
bf_acquisition_test(capture_recorder_test ../controller/capture_recorder_test.cpp bf_device_ui)
