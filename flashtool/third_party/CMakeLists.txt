# qmqtt — lightweight Qt MQTT client (MIT)
#
# Before first cmake configure, clone into this directory:
#   git clone https://github.com/emqx/qmqtt qmqtt
# or add as submodule from the project root:
#   git submodule add https://github.com/emqx/qmqtt third_party/qmqtt

set(QMQTT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qmqtt")

if(NOT EXISTS "${QMQTT_DIR}/src/mqtt")
    message(FATAL_ERROR
        "\nqmqtt not found. From the project root run:\n"
        "  git clone https://github.com/emqx/qmqtt third_party/qmqtt\n"
        "Then re-run cmake.\n"
    )
endif()

file(GLOB QMQTT_SOURCES "${QMQTT_DIR}/src/mqtt/*.cpp")
# Headers must be listed explicitly as sources so AUTOMOC generates MOC files
# for abstract-only Q_OBJECT classes (NetworkInterface etc.)
file(GLOB QMQTT_HEADERS "${QMQTT_DIR}/src/mqtt/*.h")

add_library(qmqtt STATIC ${QMQTT_SOURCES} ${QMQTT_HEADERS})

target_compile_definitions(qmqtt PUBLIC MQTT_PROJECT_INCLUDE_SRC)

target_include_directories(qmqtt PUBLIC "${QMQTT_DIR}/src/mqtt")

target_link_libraries(qmqtt PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)

set_target_properties(qmqtt PROPERTIES AUTOMOC ON)
