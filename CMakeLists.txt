cmake_minimum_required(VERSION 3.25)
project(Mawari VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Concurrent Svg LinguistTools)

add_library(mawari_core STATIC
    src/application_icon.cc
    src/application_icon.h
    src/translations.cc
    src/translations.h
    src/preferences.cc
    src/preferences.h
    src/camera.h
    src/camera.cc
    src/image_loader.cc
    src/image_loader.h
    src/image_exporter.cc
    src/image_exporter.h
    src/panorama_view.cc
    src/panorama_view.h
    src/main_window.cc
    src/main_window.h
)
target_link_libraries(mawari_core PUBLIC
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL Qt6::Concurrent
)
target_include_directories(mawari_core PUBLIC src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mawari_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Render the SVG once at build time; no GUI or SVG icon plugin is needed at runtime.
add_executable(mawari_render_icons tools/render_icons.cc)
target_include_directories(mawari_render_icons PRIVATE src)
target_link_libraries(mawari_render_icons PRIVATE Qt6::Svg)
set(MAWARI_ICON_SIZES 16 22 24 32 48 64 96 128 256 512)
set(MAWARI_ICON_FILES)
foreach(size IN LISTS MAWARI_ICON_SIZES)
    list(APPEND MAWARI_ICON_FILES "${CMAKE_CURRENT_BINARY_DIR}/icons/${size}/mawari.png")
endforeach()
add_custom_command(OUTPUT ${MAWARI_ICON_FILES}
    COMMAND mawari_render_icons "${CMAKE_CURRENT_SOURCE_DIR}/assets/mawari.svg" "${CMAKE_CURRENT_BINARY_DIR}/icons"
    DEPENDS mawari_render_icons assets/mawari.svg src/application_icon.h VERBATIM)
qt_add_resources(mawari_core mawari_icons PREFIX "/icons"
    BASE "${CMAKE_CURRENT_BINARY_DIR}/icons" FILES ${MAWARI_ICON_FILES})

# Bundle application and Qt widget translations, including in standalone installs.
get_target_property(MAWARI_QMAKE Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${MAWARI_QMAKE}" -query QT_INSTALL_TRANSLATIONS
    OUTPUT_VARIABLE MAWARI_QT_TRANSLATIONS OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
set(MAWARI_TS_FILES)
set(MAWARI_QM_FILES)
foreach(language pl ja uk fr)
    set(ts "${CMAKE_CURRENT_SOURCE_DIR}/translations/mawari_${language}.ts")
    set(qm "${CMAKE_CURRENT_BINARY_DIR}/mawari_${language}.qm")
    add_custom_command(OUTPUT "${qm}"
        COMMAND Qt6::lrelease -nounfinished "${ts}" -qm "${qm}"
        DEPENDS "${ts}" VERBATIM)
    list(APPEND MAWARI_TS_FILES "${ts}")
    list(APPEND MAWARI_QM_FILES "${qm}")
    set(qt_qm "${MAWARI_QT_TRANSLATIONS}/qtbase_${language}.qm")
    if(NOT EXISTS "${qt_qm}")
        message(FATAL_ERROR "Missing ${qt_qm}. Install the Qt 6 translations package.")
    endif()
    set_source_files_properties("${qt_qm}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_${language}.qm")
    list(APPEND MAWARI_QM_FILES "${qt_qm}")
endforeach()
qt_add_resources(mawari_core mawari_translations PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${MAWARI_QM_FILES})
add_custom_target(update_translations
    COMMAND Qt6::lupdate "${CMAKE_CURRENT_SOURCE_DIR}/src" -no-obsolete -ts ${MAWARI_TS_FILES}
    VERBATIM)

qt_add_executable(mawari src/main.cc assets/mawari.qrc)
target_link_libraries(mawari PRIVATE mawari_core Qt6::Svg)
install(TARGETS mawari RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    configure_file(cmake/install-desktop.cmake.in install-desktop.cmake @ONLY)
    install(FILES assets/mawari.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/mawari.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(size IN LISTS MAWARI_ICON_SIZES)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/icons/${size}/mawari.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps")
    endforeach()
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-desktop.cmake")
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(mawari_icon_tests tests/icon.test.cc)
    target_link_libraries(mawari_icon_tests PRIVATE mawari_core Qt6::Test)
    add_test(NAME mawari_icon_tests COMMAND mawari_icon_tests)
    set_tests_properties(mawari_icon_tests PROPERTIES
        LABELS logic
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
    qt_add_executable(mawari_localization_tests tests/localization.test.cc)
    target_link_libraries(mawari_localization_tests PRIVATE mawari_core Qt6::Test)
    target_compile_definitions(mawari_localization_tests PRIVATE
        MAWARI_TRANSLATIONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/translations")
    add_test(NAME mawari_localization_tests COMMAND mawari_localization_tests)
    set_tests_properties(mawari_localization_tests PROPERTIES
        LABELS logic
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
    qt_add_executable(mawari_tests tests/viewer.test.cc)
    target_link_libraries(mawari_tests PRIVATE mawari_core Qt6::Test)
    add_test(NAME mawari_logic_tests COMMAND mawari_tests
        fov_axis_and_limits projection_and_zoom small_sky_navigation preferences_round_trip pan_sensitivity_slider large_panorama_loading context_menu_and_panning drop_and_failed_load
        full_screen_state_changes hold_to_pan_navigation
        image_read_progress loading_indicator
        export_resolution export_projection export_files_and_failures save_image_action
    )
    set_tests_properties(mawari_logic_tests PROPERTIES
        LABELS logic
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
    add_test(NAME mawari_rendering_tests COMMAND mawari_tests rendering_and_input loading_indicator)
    set_tests_properties(mawari_rendering_tests PROPERTIES
        LABELS rendering
        ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    )
endif()
