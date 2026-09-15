cmake_minimum_required(VERSION 3.10)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MOONMIC_VERSION LIMIT_COUNT 1)
if(NOT MOONMIC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must use MAJOR.MINOR.PATCH")
endif()

project(moonmic VERSION ${MOONMIC_VERSION})

option(BUILD_CLIENT "Build the Moonmic client library" ON)
option(MOONMIC_BUILD_TESTS "Build Moonmic protocol tests" OFF)

if(MOONMIC_BUILD_TESTS)
    enable_testing()

    add_executable(moonmic-protocol-c-test tests/protocol_test.c)
    target_include_directories(moonmic-protocol-c-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME protocol-c COMMAND moonmic-protocol-c-test)

    add_executable(moonmic-protocol-cpp-test tests/protocol_test.cpp)
    target_include_directories(moonmic-protocol-cpp-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME protocol-cpp COMMAND moonmic-protocol-cpp-test)
endif()

if(NOT BUILD_CLIENT)
    return()
endif()

set(MOONMIC_SOURCES
    src/moonmic_client.cpp
    codec/opus_encoder.cpp
    network/udp_sender.cpp
)

set(MOONMIC_PLATFORM_LIBRARIES "")

if(VITA)
    list(APPEND MOONMIC_SOURCES
        platform/psvita/audio_capture_vita.cpp
        platform/psvita/heartbeat_monitor.cpp
    )
    list(APPEND MOONMIC_PLATFORM_LIBRARIES
        SceNet_stub
        SceAudio_stub
    )
elseif(WIN32)
    list(APPEND MOONMIC_SOURCES
        platform/windows/audio_capture_windows.cpp
        platform/windows/heartbeat_monitor.cpp
    )
    list(APPEND MOONMIC_PLATFORM_LIBRARIES ws2_32)
elseif(UNIX AND NOT APPLE)
    list(APPEND MOONMIC_SOURCES
        platform/linux/audio_capture_linux.cpp
        platform/linux/heartbeat_monitor.cpp
    )

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
    list(APPEND MOONMIC_PLATFORM_LIBRARIES
        pthread
        ${PULSEAUDIO_LIBRARIES}
    )
else()
    message(FATAL_ERROR "Moonmic does not support this platform")
endif()

add_library(moonmic STATIC ${MOONMIC_SOURCES})
add_library(moonmic::moonmic ALIAS moonmic)

target_compile_features(moonmic PUBLIC cxx_std_17)
target_compile_definitions(moonmic PRIVATE MOONMIC_VERSION="${PROJECT_VERSION}")

target_include_directories(moonmic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/codec
        ${CMAKE_CURRENT_SOURCE_DIR}/network
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${PULSEAUDIO_INCLUDE_DIRS}
)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(OPUS QUIET opus)
endif()

if(OPUS_FOUND)
    target_include_directories(moonmic PRIVATE ${OPUS_INCLUDE_DIRS})
    target_link_libraries(moonmic PUBLIC ${OPUS_LIBRARIES})
else()
    find_library(OPUS_LIBRARY NAMES opus)
    find_path(OPUS_INCLUDE_DIR opus/opus.h)
    if(NOT OPUS_LIBRARY OR NOT OPUS_INCLUDE_DIR)
        message(FATAL_ERROR "Opus development files were not found")
    endif()
    target_include_directories(moonmic PRIVATE ${OPUS_INCLUDE_DIR})
    target_link_libraries(moonmic PUBLIC ${OPUS_LIBRARY})
endif()

find_package(Threads REQUIRED)
target_link_libraries(moonmic PUBLIC
    Threads::Threads
    ${MOONMIC_PLATFORM_LIBRARIES}
)

if(MOONMIC_BUILD_TESTS)
    add_executable(moonmic-api-link-test tests/api_link_test.c)
    target_link_libraries(moonmic-api-link-test PRIVATE moonmic::moonmic)
    add_test(NAME api-link COMMAND moonmic-api-link-test)
endif()

install(TARGETS moonmic
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES
    include/heartbeat_monitor.h
    include/moonmic.h
    include/moonmic_protocol.h
    DESTINATION include
)
