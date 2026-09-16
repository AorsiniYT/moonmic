cmake_minimum_required(VERSION 3.15)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" MOONMIC_VERSION LIMIT_COUNT 1)
if(NOT MOONMIC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must use MAJOR.MINOR.PATCH")
endif()

project(moonmic-host VERSION ${MOONMIC_VERSION})

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED HOST_TARGET_WINDOWS AND NOT DEFINED HOST_TARGET_LINUX)
    if(WIN32)
        set(HOST_TARGET_WINDOWS ON)
    elseif(UNIX AND NOT APPLE)
        set(HOST_TARGET_LINUX ON)
    endif()
endif()

option(USE_IMGUI "Build with Dear ImGui GUI" ON)
option(MOONMIC_HOST_BUILD_TESTS "Build Moonmic host tests" OFF)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

set(SOURCES
    src/audio_resampler.cpp
    src/codec/ffmpeg_decoder.cpp
    src/network/udp_receiver.cpp
    src/network/connection_monitor.cpp
    src/config.cpp
    src/main.cpp
    src/audio_receiver.cpp
    src/sunshine_webui.cpp
    src/sunshine_request.cpp
    src/single_instance.cpp
    src/display_manager.cpp
    src/version_checker.cpp
    src/logger.h
)

if(HOST_TARGET_WINDOWS OR (WIN32 AND NOT HOST_TARGET_LINUX))
    message(STATUS "moonmic-host: Building for Windows")
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/drivers" MOONMIC_DRIVER_DIR)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/moonmic.ico" MOONMIC_ICON_ICO)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/moonmic.png" MOONMIC_ICON_PNG)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/resources.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/guardian.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/guardian.rc"
        @ONLY
    )
    list(APPEND SOURCES
        src/platform/windows/virtual_device_win.cpp
        src/platform/windows/driver_installer.cpp
        src/platform/windows/audio_utils.cpp
        src/platform/windows/audio_device_manager.cpp
        src/platform/windows/typing_focus_win.cpp
        src/guardian_state.cpp
        src/guardian_launcher.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/resources.rc
    )
elseif(HOST_TARGET_LINUX OR (UNIX AND NOT APPLE AND NOT HOST_TARGET_WINDOWS))
    message(STATUS "moonmic-host: Building for Linux")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/moonmic.png"
        "${CMAKE_CURRENT_BINARY_DIR}/moonmic.png"
        COPYONLY
    )
    list(APPEND SOURCES
        src/platform/linux/virtual_device_linux.cpp
        src/platform/linux/typing_focus_linux.cpp
    )
    set(MOONMIC_INSTALLED_ICON "${CMAKE_INSTALL_FULL_DATADIR}/pixmaps/moonmic.png")
endif()

if(USE_IMGUI)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

    if(EXISTS ${IMGUI_DIR}/imgui.cpp)
        set(IMGUI_SOURCES
            ${IMGUI_DIR}/imgui.cpp
            ${IMGUI_DIR}/imgui_draw.cpp
            ${IMGUI_DIR}/imgui_tables.cpp
            ${IMGUI_DIR}/imgui_widgets.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
            ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )
        list(APPEND SOURCES
            ${IMGUI_SOURCES}
            src/sunshine_settings_gui.cpp
            src/display_settings_gui.cpp
            src/debug_gui.cpp
            src/gui_helper.cpp
        )
    else()
        message(WARNING "ImGui submodule not found at ${IMGUI_DIR}. Disabling GUI.")
        set(USE_IMGUI OFF)
    endif()
endif()

add_executable(moonmic-host ${SOURCES})

if(USE_IMGUI)
    target_compile_definitions(moonmic-host PRIVATE USE_IMGUI)
endif()
if(MOONMIC_INSTALLED_ICON)
    target_compile_definitions(moonmic-host PRIVATE MOONMIC_INSTALLED_ICON="${MOONMIC_INSTALLED_ICON}")
endif()

add_subdirectory(third_party)

if(TARGET curl_build)
    add_dependencies(moonmic-host curl_build)
endif()
if(TARGET ffmpeg_build)
    add_dependencies(moonmic-host ffmpeg_build)
endif()
if(TARGET speexdsp)
    add_dependencies(moonmic-host speexdsp)
endif()

target_include_directories(moonmic-host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated version.h
)

if(USE_IMGUI AND EXISTS ${IMGUI_DIR}/imgui.cpp)
    target_include_directories(moonmic-host PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
endif()

find_package(Threads REQUIRED)
target_link_libraries(moonmic-host PRIVATE Threads::Threads)

if(CURL_FOUND AND CURL_LIB_PATH)
    message(STATUS "Using built CURL from submodule")
    message(STATUS "  CURL lib: ${CURL_LIB_PATH}")
    target_link_libraries(moonmic-host PRIVATE ${CURL_LIB_PATH})
    target_include_directories(moonmic-host PRIVATE ${CURL_INCLUDE_DIRS})
    if(WIN32)
        target_compile_definitions(moonmic-host PRIVATE CURL_STATICLIB)
    endif()
else()
    find_package(CURL REQUIRED)
    target_link_libraries(moonmic-host PRIVATE CURL::libcurl)
    message(STATUS "Using system CURL")
endif()

if(USE_IMGUI AND (HOST_TARGET_WINDOWS OR (WIN32 AND NOT HOST_TARGET_LINUX)))
    message(STATUS "Building moonmic-guardian watchdog executable")

    add_executable(moonmic-guardian
        ${CMAKE_CURRENT_BINARY_DIR}/guardian.rc
        guardian/main.cpp
        src/guardian_state.cpp
        src/config.cpp
        src/platform/windows/audio_device_manager.cpp
    )

    target_include_directories(moonmic-guardian PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_compile_definitions(moonmic-guardian PRIVATE USE_IMGUI)

    target_link_libraries(moonmic-guardian PRIVATE
        ole32
        oleaut32
        advapi32
        propsys
        uuid
        setupapi
        ws2_32
        winmm
    )

    if(MINGW)
        target_link_options(moonmic-guardian PRIVATE
            -mconsole
            -static-libgcc
            -static-libstdc++
            -static
        )
    endif()

    if(IMGUI_SOURCES)
        target_sources(moonmic-guardian PRIVATE ${IMGUI_SOURCES})
        target_include_directories(moonmic-guardian PRIVATE
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
        )
        target_link_libraries(moonmic-guardian PRIVATE opengl32)
    endif()

    set(JSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)
    if(EXISTS ${JSON_DIR}/include/nlohmann/json.hpp)
        target_include_directories(moonmic-guardian PRIVATE ${JSON_DIR}/include)
    else()
        message(FATAL_ERROR "nlohmann/json not found in ${JSON_DIR}")
    endif()

    set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw)
    if(EXISTS ${GLFW_DIR}/CMakeLists.txt)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        if(NOT TARGET glfw)
            add_subdirectory(${GLFW_DIR} glfw)
        endif()
        target_link_libraries(moonmic-guardian PRIVATE glfw)
    else()
        message(FATAL_ERROR "GLFW not found in ${GLFW_DIR}")
    endif()
endif()

if(WIN32)
    target_link_libraries(moonmic-host PRIVATE
        ws2_32     # Winsock2
        wldap32    # Windows LDAP (needed by CURL)
        iphlpapi   # IP Helper API (if_nametoindex for CURL)
        crypt32    # Windows Crypto API (SChannel)
        bcrypt     # Windows crypto primitives (SChannel)
    )
    message(STATUS "Linked Windows system libraries for CURL with SChannel")
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(moonmic-host PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "Linked system OpenSSL for CURL HTTPS support")
endif()

set(JSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)
if(EXISTS ${JSON_DIR}/include/nlohmann/json.hpp)
    target_include_directories(moonmic-host PRIVATE ${JSON_DIR}/include)
else()
    message(FATAL_ERROR "nlohmann/json not found in ${JSON_DIR}")
endif()

if(USE_IMGUI AND (HOST_TARGET_WINDOWS OR (WIN32 AND NOT HOST_TARGET_LINUX)))
    set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw)
    if(EXISTS ${GLFW_DIR}/CMakeLists.txt)
        if(NOT TARGET glfw)
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
            add_subdirectory(${GLFW_DIR} glfw)
        endif()
        target_link_libraries(moonmic-host PRIVATE glfw)
    else()
        message(FATAL_ERROR "GLFW not found in ${GLFW_DIR}")
    endif()
elseif(USE_IMGUI AND (HOST_TARGET_LINUX OR (UNIX AND NOT APPLE AND NOT HOST_TARGET_WINDOWS)))
    find_package(glfw3 REQUIRED)
endif()

message(STATUS "Configuring FFmpeg static build...")

target_link_libraries(moonmic-host PRIVATE ${FFMPEG_LIBRARIES})
target_include_directories(moonmic-host PRIVATE ${FFMPEG_INCLUDE_DIRS})

if(HOST_TARGET_WINDOWS OR (WIN32 AND NOT HOST_TARGET_LINUX))
    target_link_libraries(moonmic-host PRIVATE
        bcrypt      # For BCrypt* functions (random number generation)
        secur32     # For security functions
    )
endif()

message(STATUS "FFmpeg enabled (static build from submodule)")

if(HOST_TARGET_WINDOWS OR (WIN32 AND NOT HOST_TARGET_LINUX))
    set(SPEEX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/speexdsp)
    if(EXISTS ${SPEEX_DIR}/libspeexdsp/resample.c)
        add_library(speexdsp STATIC
            ${SPEEX_DIR}/libspeexdsp/resample.c
        )

        target_include_directories(speexdsp PUBLIC
            ${SPEEX_DIR}/include
        )

        target_compile_definitions(speexdsp PRIVATE
            EXPORT=
            FLOATING_POINT
            USE_SSE
            USE_SSE2
        )

        target_link_libraries(moonmic-host PRIVATE speexdsp)
        target_include_directories(moonmic-host PRIVATE ${SPEEX_DIR}/include)
        message(STATUS "Speex resampler enabled (built from source)")
    else()
        message(WARNING "Speex submodule not found - audio quality may be degraded")
    endif()
else()
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(SPEEXDSP speexdsp)
    endif()

    if(SPEEXDSP_FOUND)
        target_link_libraries(moonmic-host PRIVATE ${SPEEXDSP_LIBRARIES})
        target_include_directories(moonmic-host PRIVATE ${SPEEXDSP_INCLUDE_DIRS})
        message(STATUS "Speex resampler enabled (system library)")
    else()
        message(WARNING "Speex library not found - install libspeexdsp-dev")
    endif()
endif()

if(HOST_TARGET_WINDOWS AND PORTAUDIO_LIBRARIES)
    target_link_libraries(moonmic-host PRIVATE ${PORTAUDIO_LIBRARIES})
    target_include_directories(moonmic-host PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_compile_definitions(moonmic-host PRIVATE USE_PORTAUDIO)
    message(STATUS "PortAudio integration enabled")

    target_sources(moonmic-host PRIVATE
        src/platform/portaudio/virtual_device_pa.cpp
    )
endif()

if(HOST_TARGET_WINDOWS OR (WIN32 AND NOT HOST_TARGET_LINUX))
    target_link_libraries(moonmic-host PRIVATE
        ws2_32
        winmm
        ole32
        propsys
        setupapi
        newdev      # For UpdateDriverForPlugAndPlayDevices
        uuid        # For GUID/UUID references (IPolicyConfig)
        oleaut32    # For SysAllocString/SysFreeString (BSTR operations)
    )

    if(MINGW)
        target_link_options(moonmic-host PRIVATE
            -mconsole
            -static-libgcc
            -static-libstdc++
            -static
        )
    endif()

    if(USE_IMGUI)
        target_link_libraries(moonmic-host PRIVATE opengl32)
    endif()

elseif(HOST_TARGET_LINUX OR (UNIX AND NOT APPLE AND NOT HOST_TARGET_WINDOWS))
    pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
    target_link_libraries(moonmic-host PRIVATE ${PULSEAUDIO_LIBRARIES})
    target_include_directories(moonmic-host PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})

    if(USE_IMGUI)
        find_package(OpenGL REQUIRED)
        target_link_libraries(moonmic-host PRIVATE glfw OpenGL::GL ${CMAKE_DL_LIBS})
    endif()
endif()

if(MOONMIC_HOST_BUILD_TESTS)
    enable_testing()
    add_executable(moonmic-audio-resampler-test
        tests/audio_resampler_test.cpp
        src/audio_resampler.cpp
    )
    target_include_directories(moonmic-audio-resampler-test PRIVATE src)
    if(TARGET speexdsp)
        target_link_libraries(moonmic-audio-resampler-test PRIVATE speexdsp)
    elseif(SPEEXDSP_FOUND)
        target_include_directories(moonmic-audio-resampler-test PRIVATE ${SPEEXDSP_INCLUDE_DIRS})
        target_link_libraries(moonmic-audio-resampler-test PRIVATE ${SPEEXDSP_LIBRARIES})
    else()
        message(FATAL_ERROR "SpeexDSP is required for host tests")
    endif()
    add_test(NAME audio-resampler-rate-change COMMAND moonmic-audio-resampler-test)

    add_executable(moonmic-config-test
        tests/config_test.cpp
        src/config.cpp
    )
    target_include_directories(moonmic-config-test PRIVATE src ${JSON_DIR}/include)
    if(WIN32)
        target_link_libraries(moonmic-config-test PRIVATE shell32)
    endif()
    add_test(NAME config-roundtrip COMMAND moonmic-config-test)

    add_executable(moonmic-udp-lifecycle-test
        tests/udp_lifecycle_test.cpp
        src/network/udp_receiver.cpp
    )
    target_include_directories(moonmic-udp-lifecycle-test PRIVATE src)
    target_link_libraries(moonmic-udp-lifecycle-test PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(moonmic-udp-lifecycle-test PRIVATE ws2_32)
    endif()
    add_test(NAME udp-start-stop-restart COMMAND moonmic-udp-lifecycle-test)

    add_executable(moonmic-sunshine-request-test
        tests/sunshine_request_test.cpp
        src/sunshine_request.cpp
    )
    target_include_directories(moonmic-sunshine-request-test PRIVATE src)
    add_test(NAME sunshine-tls-auth COMMAND moonmic-sunshine-request-test)

    add_executable(moonmic-logger-test tests/logger_test.cpp)
    target_include_directories(moonmic-logger-test PRIVATE src)
    target_link_libraries(moonmic-logger-test PRIVATE Threads::Threads)
    add_test(NAME logger-concurrent-writes COMMAND moonmic-logger-test)
endif()

install(TARGETS moonmic-host DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/moonmic-host.json.example DESTINATION ${CMAKE_INSTALL_SYSCONFDIR} RENAME moonmic-host.json)

if(HOST_TARGET_LINUX OR (UNIX AND NOT APPLE AND NOT HOST_TARGET_WINDOWS))
    install(FILES resources/linux/moonmic-host.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/moonmic.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/moonmic-host.json.example
    ${CMAKE_BINARY_DIR}/moonmic-host.json
    COPYONLY
)
