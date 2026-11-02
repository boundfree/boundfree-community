# Keep the UI dependency reproducible across Windows, Linux and CI.
include(FetchContent)
set(FLUENT_QT_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_SPATIAL OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_QML_BRIDGE OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_PARTICLES OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_PYSIDE6_BINDINGS OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(bf_fluentqt
    GIT_REPOSITORY https://github.com/calvinhxx/Fluent-Qt.git
    GIT_TAG 766e13c83599e8ca58f83e7c4917c66ae73aa559
)
FetchContent_MakeAvailable(bf_fluentqt)
# Resources compiled into the static core include fonts and the icon face.
foreach(asset IN ITEMS LICENSE THIRD_PARTY_NOTICES.md
    third_party/fonts/inter/LICENSE.txt
    third_party/icons/fluentui-system-icons/LICENSE.txt
    third_party/icons/fluentui-system-icons/NOTICE.txt)
    get_filename_component(asset_dir "${asset}" DIRECTORY)
    install(FILES "${bf_fluentqt_SOURCE_DIR}/${asset}"
        DESTINATION "legal/fluentqt/${asset_dir}")
endforeach()
