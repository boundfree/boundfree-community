# Select the Community toolchain without changing other projects' Qt aliases.
if(DEFINED ENV{BF_QT_ROOT} AND NOT "$ENV{BF_QT_ROOT}" STREQUAL "")
    set(_bf_qt_root "$ENV{BF_QT_ROOT}")
else()
    execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
        OUTPUT_VARIABLE _bf_git_dir OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _bf_git_result)
    if(NOT _bf_git_result EQUAL 0)
        message(FATAL_ERROR "Set BF_QT_ROOT to the Community Qt installation.")
    endif()
    get_filename_component(_bf_workspace "${_bf_git_dir}/../.." ABSOLUTE)
    set(_bf_qt_root "${_bf_workspace}/toolchains/qt6-community")
endif()
if(NOT EXISTS "${_bf_qt_root}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "Community Qt not found at ${_bf_qt_root}. Set BF_QT_ROOT; see docs/toolchain-wsl2.md.")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${_bf_qt_root}")
if(EXISTS "${_bf_qt_root}/bin/x86_64-conda-linux-gnu-c++")
    set(CMAKE_CXX_COMPILER "${_bf_qt_root}/bin/x86_64-conda-linux-gnu-c++" CACHE FILEPATH "Community C++ compiler")
endif()
