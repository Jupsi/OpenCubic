cmake_minimum_required(VERSION 3.20)
project(AceFlashTool VERSION 1.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt 5.15.2 msvc2019_64 — override with -DCMAKE_PREFIX_PATH=<path>
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "E:/Qt/5.15.2/msvc2019_64")
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

add_subdirectory(third_party)

add_executable(AceFlashTool WIN32
    main.cpp
    core/discovery.cpp
    core/firmware_file.cpp
    core/firmware_packager.cpp
    core/http_server.cpp
    core/mqtt_client.cpp
    targets/ams_target.cpp
    targets/printer_app_target.cpp
    ui/mainwindow.cpp
    ui/discovery_widget.cpp
    ui/flash_widget.cpp
    ui/mainwindow.ui
    ui/discovery_widget.ui
    ui/flash_widget.ui
)

target_include_directories(AceFlashTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AceFlashTool PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
    qmqtt
    Bcrypt       # Windows AES-128-CBC for Discovery
)

# OpenSSL 1.1.1 DLLs — not bundled with Qt 5.15 MSVC; typically found in Qt Creator's bin dir
# Override with -DOPENSSL_SRC=<path> if your installation differs
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
if(WIN32)
    get_target_property(_qmake Qt5::qmake IMPORTED_LOCATION)
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
