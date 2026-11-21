cmake_minimum_required(VERSION 3.20)
project(AceFlashTool VERSION 1.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# BUILD_GUI: Qt Widgets desktop app (AceFlashTool)
# BUILD_CLI: headless command-line flasher (aceflash) — Linux/macOS/Windows
if(WIN32)
    option(BUILD_GUI "Build the Qt GUI flash tool" ON)
else()
    option(BUILD_GUI "Build the Qt GUI flash tool" OFF)
endif()
option(BUILD_CLI "Build the aceflash command-line tool" ON)
option(BUILD_TESTING "Build the QtTest unit tests" ON)

# Qt 5.15.2 msvc2019_64 — override with -DCMAKE_PREFIX_PATH=<path>
if(WIN32 AND NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "E:/Qt/5.15.2/msvc2019_64")
endif()

set(_qt_components Core Network)
if(BUILD_GUI)
    list(APPEND _qt_components Gui Widgets)
endif()
if(BUILD_TESTING)
    list(APPEND _qt_components Test)
endif()

# Prefer Qt6 on Linux, Qt5 elsewhere; force with -DACEFLASH_QT_MAJOR=5|6
if(NOT DEFINED ACEFLASH_QT_MAJOR)
    if(WIN32)
        set(ACEFLASH_QT_MAJOR 5)
    else()
        set(ACEFLASH_QT_MAJOR 6)
    endif()
endif()
find_package(Qt${ACEFLASH_QT_MAJOR} QUIET COMPONENTS ${_qt_components})
if(NOT Qt${ACEFLASH_QT_MAJOR}_FOUND)
    if(ACEFLASH_QT_MAJOR EQUAL 6)
        set(ACEFLASH_QT_MAJOR 5)
    else()
        set(ACEFLASH_QT_MAJOR 6)
    endif()
    find_package(Qt${ACEFLASH_QT_MAJOR} REQUIRED COMPONENTS ${_qt_components})
endif()
set(QT_VERSION_MAJOR ${ACEFLASH_QT_MAJOR})
message(STATUS "Using Qt${QT_VERSION_MAJOR}")

if(NOT WIN32)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
endif()

add_subdirectory(third_party)

# ── Shared core (no widgets) ────────────────────────────────────────────────
add_library(aceflash_core STATIC
    core/crypto.cpp
    core/discovery.cpp
    core/firmware_file.cpp
    core/firmware_packager.cpp
    core/flash_session.cpp
    core/http_server.cpp
    core/mqtt_client.cpp
    targets/ams_target.cpp
    targets/printer_app_target.cpp
)
target_include_directories(aceflash_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aceflash_core PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    qmqtt
)
if(WIN32)
    target_link_libraries(aceflash_core PUBLIC Bcrypt)   # Windows AES-128-CBC for Discovery
else()
    target_link_libraries(aceflash_core PUBLIC OpenSSL::Crypto)
endif()

# ── CLI ─────────────────────────────────────────────────────────────────────
if(BUILD_CLI)
    add_executable(aceflash cli/main.cpp)
    target_link_libraries(aceflash PRIVATE aceflash_core)
    target_compile_definitions(aceflash PRIVATE
        ACEFLASH_VERSION="${PROJECT_VERSION}")
    include(GNUInstallDirs)
    install(TARGETS aceflash RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── Tests ───────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    enable_testing()
    add_executable(tst_core tests/tst_core.cpp)
    target_link_libraries(tst_core PRIVATE aceflash_core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME tst_core COMMAND tst_core)

    if(BUILD_GUI)
        # Headless GUI smoke test (offscreen platform; no display needed).
        # Set ACEFLASH_SCREENSHOT_DIR to save a PNG of the main window.
        add_executable(tst_gui tests/tst_gui.cpp)
        target_link_libraries(tst_gui PRIVATE aceflash_ui Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME tst_gui COMMAND tst_gui)
        set_tests_properties(tst_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

# ── GUI ─────────────────────────────────────────────────────────────────────
if(BUILD_GUI)
    # Widgets as a library so the GUI smoke test can link them too
    add_library(aceflash_ui STATIC
        ui/mainwindow.cpp
        ui/discovery_widget.cpp
        ui/flash_widget.cpp
        ui/mainwindow.ui
        ui/discovery_widget.ui
        ui/flash_widget.ui
    )
    target_link_libraries(aceflash_ui PUBLIC
        aceflash_core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    add_executable(AceFlashTool WIN32 main.cpp)
    target_link_libraries(AceFlashTool PRIVATE aceflash_ui)
endif()

# OpenSSL 1.1.1 DLLs — not bundled with Qt 5.15 MSVC; typically found in Qt Creator's bin dir
# Override with -DOPENSSL_SRC=<path> if your installation differs
if(WIN32 AND BUILD_GUI)
    if(NOT DEFINED OPENSSL_SRC)
        set(OPENSSL_SRC "E:/Qt/Tools/QtCreator/bin")
    endif()
    foreach(SSL_DLL libssl-1_1-x64.dll libcrypto-1_1-x64.dll)
        if(EXISTS "${OPENSSL_SRC}/${SSL_DLL}")
            add_custom_command(TARGET AceFlashTool POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENSSL_SRC}/${SSL_DLL}"
                    "$<TARGET_FILE_DIR:AceFlashTool>/${SSL_DLL}"
                COMMENT "Deploying ${SSL_DLL}"
            )
        endif()
    endforeach()

    # Copy Qt DLLs to output directory after build
    get_target_property(_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT)
        add_custom_command(TARGET AceFlashTool POST_BUILD
            COMMAND "${WINDEPLOYQT}"
                --no-translations
                --no-opengl-sw
                --no-angle
                $<TARGET_FILE:AceFlashTool>
            COMMENT "Deploying Qt DLLs"
        )
        # Remove files not needed for this app
        add_custom_command(TARGET AceFlashTool POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:AceFlashTool>/imageformats"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:AceFlashTool>/translations"
            COMMAND ${CMAKE_COMMAND} -E remove -f
                "$<TARGET_FILE_DIR:AceFlashTool>/opengl32sw.dll"
                "$<TARGET_FILE_DIR:AceFlashTool>/libGLESv2.dll"
                "$<TARGET_FILE_DIR:AceFlashTool>/libEGL.dll"
                "$<TARGET_FILE_DIR:AceFlashTool>/D3Dcompiler_47.dll"
            COMMENT "Removing unused OpenGL/ANGLE/imageformats/translations"
        )
    endif()
endif()
