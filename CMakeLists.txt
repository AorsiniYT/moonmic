cmake_minimum_required(VERSION 3.10)

project(moonmic VERSION 1.0.0)

option(BUILD_CLIENT "Build client library" ON)
option(BUILD_HOST "Build host application" ON)
option(BUILD_HOST_WINDOWS "Build Windows host (cross-compile)" OFF)
option(BUILD_HOST_LINUX "Build Linux host (cross-compile)" OFF)

if(BUILD_CLIENT)
    message(STATUS "=== Building moonmic client library ===")

    add_library(moonmic STATIC
        moonmic_client.cpp
        codec/opus_encoder.cpp
        network/udp_sender.cpp
    )

    if(VITA)
        message(STATUS "moonmic: Building for PS Vita")
        target_sources(moonmic PRIVATE platform/psvita/audio_capture_vita.cpp)
        target_sources(moonmic PRIVATE platform/psvita/heartbeat_monitor.cpp)
        target_link_libraries(moonmic PUBLIC
            SceNet_stub
            SceAudio_stub
        )

    elseif(WIN32)
        message(STATUS "moonmic: Building for Windows")
        target_sources(moonmic PRIVATE platform/windows/audio_capture_windows.cpp)
        target_sources(moonmic PRIVATE platform/windows/heartbeat_monitor.cpp)
        target_link_libraries(moonmic PUBLIC ws2_32)

    elseif(UNIX AND NOT APPLE)
        message(STATUS "moonmic: Building for Linux")
        target_sources(moonmic PRIVATE platform/linux/audio_capture_linux.cpp)
        target_sources(moonmic PRIVATE platform/linux/heartbeat_monitor.cpp)
        target_link_libraries(moonmic PUBLIC pthread)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
        target_link_libraries(moonmic PUBLIC ${PULSEAUDIO_LIBRARIES})
        target_include_directories(moonmic PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})

    elseif(APPLE)
        message(STATUS "moonmic: macOS support not yet implemented")

    elseif(ANDROID)
        message(STATUS "moonmic: Android support not yet implemented")

    else()
        message(FATAL_ERROR "moonmic: Unsupported platform")
    endif()

    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS opus)
    endif()

    if(OPUS_FOUND)
        message(STATUS "moonmic: Found Opus via pkg-config")
        target_link_libraries(moonmic PUBLIC ${OPUS_LIBRARIES})
        target_include_directories(moonmic PRIVATE ${OPUS_INCLUDE_DIRS})
    else()
        find_library(OPUS_LIBRARY NAMES opus)
        find_path(OPUS_INCLUDE_DIR opus/opus.h)

        if(OPUS_LIBRARY AND OPUS_INCLUDE_DIR)
            message(STATUS "moonmic: Found Opus library: ${OPUS_LIBRARY}")
            target_link_libraries(moonmic PUBLIC ${OPUS_LIBRARY})
            target_include_directories(moonmic PRIVATE ${OPUS_INCLUDE_DIR})
        else()
            message(WARNING "moonmic: Opus library not found. Please install libopus-dev or equivalent.")
            message(WARNING "  - Debian/Ubuntu: sudo apt-get install libopus-dev")
            message(WARNING "  - Fedora/RHEL: sudo dnf install opus-devel")
            message(WARNING "  - Windows: Download from https://opus-codec.org/")
            message(WARNING "  - PS Vita: Should be included in VITASDK")
        endif()
    endif()

    target_include_directories(moonmic PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_include_directories(moonmic PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/codec
        ${CMAKE_CURRENT_SOURCE_DIR}/network
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
    )

    set_target_properties(moonmic PROPERTIES CXX_STANDARD 17)

    if(WIN32)
        target_link_libraries(moonmic PUBLIC ws2_32)
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries(moonmic PUBLIC Threads::Threads)

    install(TARGETS moonmic
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )

    install(FILES moonmic.h
        DESTINATION include
    )
endif()

message(STATUS "")
message(STATUS "=== moonmic Build Configuration ===")
message(STATUS "Client library: ${BUILD_CLIENT}")
message(STATUS "Host application: ${BUILD_HOST}")
if(BUILD_HOST)
    message(STATUS "Host platforms: ${BUILD_HOSTS}")
    message(STATUS "Output directories:")
    foreach(HOST_PLATFORM ${BUILD_HOSTS})
        message(STATUS "  - ${CMAKE_BINARY_DIR}/host-${HOST_PLATFORM}")
    endforeach()
endif()
message(STATUS "======================================")
message(STATUS "")
