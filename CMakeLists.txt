cmake_minimum_required(VERSION 3.10)

project(moonmic VERSION 1.0.0)

# Options
option(BUILD_CLIENT "Build client library" ON)
option(BUILD_HOST "Build host application" ON)
option(BUILD_HOST_WINDOWS "Build Windows host (cross-compile)" OFF)
option(BUILD_HOST_LINUX "Build Linux host (cross-compile)" OFF)

# ============================================================================
# CLIENT LIBRARY
# ============================================================================

if(BUILD_CLIENT)
    message(STATUS "=== Building moonmic client library ===")
    
    # Build as static library
    add_library(moonmic STATIC
        moonmic_client.cpp
        codec/opus_encoder.cpp
        network/udp_sender.cpp
    )
    
    # Add platform-specific sources
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
        
        # Find PulseAudio
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
        target_link_libraries(moonmic PUBLIC ${PULSEAUDIO_LIBRARIES})
        target_include_directories(moonmic PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})
        
    elseif(APPLE)
        message(STATUS "moonmic: macOS support not yet implemented")
        # TODO: Implement platform/macos/audio_capture_macos.cpp
        # TODO: Implement platform/macos/heartbeat_monitor.cpp
        
    elseif(ANDROID)
        message(STATUS "moonmic: Android support not yet implemented")
        # TODO: Implement platform/android/audio_capture_android.cpp
        # TODO: Implement platform/android/heartbeat_monitor.cpp
        
    else()
        message(FATAL_ERROR "moonmic: Unsupported platform")
    endif()
    
    # Find Opus library
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS opus)
    endif()
    
    if(OPUS_FOUND)
        message(STATUS "moonmic: Found Opus via pkg-config")
        target_link_libraries(moonmic PUBLIC ${OPUS_LIBRARIES})
        target_include_directories(moonmic PRIVATE ${OPUS_INCLUDE_DIRS})
    else()
        # Fallback: try to find opus library directly
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
    
    # Public headers
    target_include_directories(moonmic PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # Private headers
    target_include_directories(moonmic PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/codec
        ${CMAKE_CURRENT_SOURCE_DIR}/network
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
    )
    
    # C++17 standard
    set_target_properties(moonmic PROPERTIES CXX_STANDARD 17)
    
    # Platform-specific networking libraries
    if(WIN32)
        target_link_libraries(moonmic PUBLIC ws2_32)
    endif()
    
    # Threading support
    find_package(Threads REQUIRED)
    target_link_libraries(moonmic PUBLIC Threads::Threads)
    
    # Installation (optional, for standalone use)
    install(TARGETS moonmic
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
    
    install(FILES moonmic.h
        DESTINATION include
    )
endif()

# ============================================================================
# HOST APPLICATIONS (Cross-compile helper) - DISABLED
# ============================================================================

# Note: Host builds are now triggered by makefast/makepsv scripts at the end
# This prevents Windows MinGW linker errors from blocking PS Vita builds

# if(BUILD_HOST AND (PLATFORM_PSV OR CMAKE_SYSTEM_NAME STREQUAL "Vita"))
#     message(STATUS "moonmic: Adding target to build host applications (Windows/Linux)")
#     
#     add_custom_target(moonmic_hosts ALL
#         COMMAND chmod +x ${CMAKE_CURRENT_SOURCE_DIR}/host/scripts/build_hosts.sh
#         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/host/scripts/build_hosts.sh
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         COMMENT "Building MoonMic host applications..."
#         VERBATIM
#     )
#     
#     # Ensure it runs after client lib is built (optional, but good for ordering)
#     add_dependencies(moonmic_hosts moonmic)
# endif()

# ============================================================================
# HOST APPLICATIONS (Disabled - handled by makefast/makepsv scripts)
# ============================================================================

# Note: Host builds are now triggered by makefast/makepsv scripts
# This prevents compilation failures in hosts from blocking PS Vita builds

# if(BUILD_HOST)
#     message(STATUS "=== Building moonmic-host application ===")
#     
#     # Determine which hosts to build
#     set(BUILD_HOSTS "")
#     
#     if(BUILD_HOST_WINDOWS)
#         list(APPEND BUILD_HOSTS "windows")
#     endif()
#     
#     if(BUILD_HOST_LINUX)
#         list(APPEND BUILD_HOSTS "linux")
#     endif()
#     
#     # If no specific host selected, build for current platform
#     if(NOT BUILD_HOSTS)
#         if(WIN32)
#             list(APPEND BUILD_HOSTS "windows")
#         elseif(UNIX AND NOT APPLE)
#             list(APPEND BUILD_HOSTS "linux")
#         endif()
#     endif()
#     
#     # Build each host
#     foreach(HOST_PLATFORM ${BUILD_HOSTS})
#         message(STATUS "Configuring host for: ${HOST_PLATFORM}")
#         
#         # Set output directory
#         set(HOST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/host-${HOST_PLATFORM}")
#         
#         # Add subdirectory with custom binary dir
#         add_subdirectory(host ${HOST_OUTPUT_DIR})
#         
#         # Set platform-specific variables for host build
#         if(HOST_PLATFORM STREQUAL "windows")
#             set(HOST_TARGET_WINDOWS ON CACHE BOOL "" FORCE)
#             set(HOST_TARGET_LINUX OFF CACHE BOOL "" FORCE)
#         elseif(HOST_PLATFORM STREQUAL "linux")
#             set(HOST_TARGET_WINDOWS OFF CACHE BOOL "" FORCE)
#             set(HOST_TARGET_LINUX ON CACHE BOOL "" FORCE)
#         endif()
#     endforeach()
# endif()

# ============================================================================
# SUMMARY
# ============================================================================

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
