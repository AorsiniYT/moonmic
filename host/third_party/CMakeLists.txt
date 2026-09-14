cmake_minimum_required(VERSION 3.15)

include(ExternalProject)
include(ProcessorCount)

# Get number of CPU cores for parallel builds
ProcessorCount(NPROC)
if(NOT NPROC OR NPROC EQUAL 0)
    set(NPROC 4)
endif()
message(STATUS "Using ${NPROC} parallel jobs for external projects")

# Determine platform
if(WIN32)
    set(BUILD_PLATFORM "windows")
    set(CROSS_PREFIX "x86_64-w64-mingw32-")
    # Toolchain file for CMake-based projects
    set(TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain-mingw.cmake")
else()
    set(BUILD_PLATFORM "linux")
    set(CROSS_PREFIX "")
    set(TOOLCHAIN_FILE "")  # Empty for native Linux builds
endif()

# CURL

# CURL directories
set(CURL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/curl)
set(CURL_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/curl-build-${BUILD_PLATFORM})
set(CURL_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/curl-install-${BUILD_PLATFORM})

message(STATUS "=== CURL Static Build Configuration ===")
message(STATUS "Platform: ${BUILD_PLATFORM}")
message(STATUS "Source: ${CURL_SOURCE_DIR}")
message(STATUS "Install: ${CURL_INSTALL_DIR}")

# Check if CURL submodule exists
if(NOT EXISTS "${CURL_SOURCE_DIR}/CMakeLists.txt")
    message(WARNING 
        "CURL source not found at ${CURL_SOURCE_DIR}\n"
    )
    set(CURL_AVAILABLE FALSE)
else()
    set(CURL_AVAILABLE TRUE)
    
    # Configure CURL build with native TLS support
    # Windows: Use SChannel (native), Linux: Use system OpenSSL
    if(WIN32)
        set(CURL_SSL_ARGS
            -DCURL_USE_SCHANNEL=ON
            -DCURL_USE_OPENSSL=OFF
            -DCURL_USE_MBEDTLS=OFF
            -DCURL_USE_WOLFSSL=OFF
            -DCURL_USE_GNUTLS=OFF
            -DCURL_USE_SECTRANSP=OFF
        )
    else()
        set(CURL_SSL_ARGS
            -DCURL_USE_OPENSSL=ON
            -DCURL_USE_SCHANNEL=OFF
            -DCURL_USE_MBEDTLS=OFF
            -DCURL_USE_WOLFSSL=OFF
            -DCURL_USE_GNUTLS=OFF
            -DCURL_USE_SECTRANSP=OFF
        )
    endif()
    
    ExternalProject_Add(curl_build
        SOURCE_DIR ${CURL_SOURCE_DIR}
        BINARY_DIR ${CURL_BUILD_DIR}
        CMAKE_ARGS
            $<$<BOOL:${TOOLCHAIN_FILE}>:-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}>
            -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_STATIC_LIBS=ON
            -DBUILD_CURL_EXE=OFF
            -DBUILD_TESTING=OFF
            ${CURL_SSL_ARGS}
            # Disable unnecessary features for minimal build
            -DCURL_DISABLE_LDAP=ON
            -DCURL_DISABLE_LDAPS=ON
            -DCURL_DISABLE_TELNET=ON
            -DCURL_DISABLE_DICT=ON
            -DCURL_DISABLE_FILE=ON
            -DCURL_DISABLE_TFTP=ON
            -DCURL_DISABLE_RTSP=ON
            -DCURL_DISABLE_POP3=ON
            -DCURL_DISABLE_IMAP=ON
            -DCURL_DISABLE_SMTP=ON
            -DCURL_DISABLE_GOPHER=ON
            -DCURL_USE_LIBPSL=OFF
            -DCURL_USE_LIBIDN2=OFF
            -DHTTP_ONLY=ON
            -DCURL_ZLIB=OFF
            -DUSE_NGHTTP2=OFF
            -DCURL_BROTLI=OFF
            -DCURL_ZSTD=OFF
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release --parallel ${NPROC}
        INSTALL_COMMAND ${CMAKE_COMMAND} --install . --config Release
        BUILD_BYPRODUCTS
            ${CURL_INSTALL_DIR}/lib/libcurl.a
    )
    
    # Set library path
    set(CURL_LIB_PATH "${CURL_INSTALL_DIR}/lib/libcurl.a")
    
    # Create install directory structure
    file(MAKE_DIRECTORY ${CURL_INSTALL_DIR}/lib)
    file(MAKE_DIRECTORY ${CURL_INSTALL_DIR}/include)
    
    # Export for parent CMakeLists - use absolute path directly
    set(CURL_INCLUDE_DIRS ${CURL_INSTALL_DIR}/include PARENT_SCOPE)
    set(CURL_LIB_PATH ${CURL_LIB_PATH} PARENT_SCOPE)
    set(CURL_FOUND TRUE PARENT_SCOPE)
    
    message(STATUS "CURL will be built statically with platform TLS")
endif()

message(STATUS "==========================================")

# FFmpeg

# Determine platform
if(WIN32)
    set(FFMPEG_PLATFORM "windows")
    set(FFMPEG_CROSS_PREFIX "x86_64-w64-mingw32-")
    set(FFMPEG_TARGET_OS "mingw32")
    set(FFMPEG_ARCH "x86_64")
else()
    set(FFMPEG_PLATFORM "linux")
    set(FFMPEG_CROSS_PREFIX "")
    set(FFMPEG_TARGET_OS "linux")
    set(FFMPEG_ARCH "x86_64")
endif()

# Output directories
set(FFMPEG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)
set(FFMPEG_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-build-${FFMPEG_PLATFORM})
set(FFMPEG_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-install-${FFMPEG_PLATFORM})

message(STATUS "=== FFmpeg Static Build Configuration ===")
message(STATUS "Platform: ${FFMPEG_PLATFORM}")
message(STATUS "Source: ${FFMPEG_SOURCE_DIR}")
message(STATUS "Build: ${FFMPEG_BUILD_DIR}")
message(STATUS "Install: ${FFMPEG_INSTALL_DIR}")

# Check if ffmpeg submodule exists
if(NOT EXISTS "${FFMPEG_SOURCE_DIR}/configure")
    message(FATAL_ERROR 
        "FFmpeg source not found at ${FFMPEG_SOURCE_DIR}\n"
        "Please run: git submodule update --init --recursive"
    )
endif()

# Configure command for FFmpeg
set(FFMPEG_CONFIGURE_CMD
    ${FFMPEG_SOURCE_DIR}/configure
    --prefix=${FFMPEG_INSTALL_DIR}
    --enable-static
    --disable-shared
    --disable-programs          # Don't build ffmpeg/ffprobe executables
    --disable-doc
    --disable-htmlpages
    --disable-manpages
    --disable-podpages
    --disable-txtpages
    --disable-network
    --disable-everything        # Disable all, then enable only what we need
    --enable-protocol=file
    --enable-decoder=opus
    --enable-decoder=pcm_s16le
    --enable-encoder=pcm_s16le
    --enable-demuxer=ogg
    --enable-demuxer=wav
    --enable-muxer=wav
    --enable-parser=opus
    --disable-autodetect
    --disable-vulkan
    --disable-v4l2-m2m
    --disable-vaapi
    --disable-vdpau
)

# Platform-specific configuration
if(WIN32)
    list(APPEND FFMPEG_CONFIGURE_CMD
        --arch=${FFMPEG_ARCH}
        --target-os=${FFMPEG_TARGET_OS}
        --cross-prefix=${FFMPEG_CROSS_PREFIX}
        --pkg-config=pkg-config
    )
endif()

# Use ExternalProject to build FFmpeg
ExternalProject_Add(ffmpeg_build
    SOURCE_DIR ${FFMPEG_SOURCE_DIR}
    BINARY_DIR ${FFMPEG_BUILD_DIR}
    CONFIGURE_COMMAND ${FFMPEG_CONFIGURE_CMD}
    BUILD_COMMAND make -j${NPROC}
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS
        ${FFMPEG_INSTALL_DIR}/lib/libavcodec.a
        ${FFMPEG_INSTALL_DIR}/lib/libavformat.a
        ${FFMPEG_INSTALL_DIR}/lib/libavutil.a
        ${FFMPEG_INSTALL_DIR}/lib/libswresample.a
)

# Create install directory structure to avoid CMake errors
file(MAKE_DIRECTORY ${FFMPEG_INSTALL_DIR}/lib)
file(MAKE_DIRECTORY ${FFMPEG_INSTALL_DIR}/include)

# Create imported targets for the static libraries (GLOBAL so they're visible in parent)
add_library(FFmpeg::avcodec STATIC IMPORTED GLOBAL)
add_library(FFmpeg::avformat STATIC IMPORTED GLOBAL)
add_library(FFmpeg::avutil STATIC IMPORTED GLOBAL)
add_library(FFmpeg::swresample STATIC IMPORTED GLOBAL)

set_target_properties(FFmpeg::avcodec PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL_DIR}/lib/libavcodec.a
)

set_target_properties(FFmpeg::avformat PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL_DIR}/lib/libavformat.a
)

set_target_properties(FFmpeg::avutil PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL_DIR}/lib/libavutil.a
)

set_target_properties(FFmpeg::swresample PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL_DIR}/lib/libswresample.a
)

# Make sure libraries are built before they're used
add_dependencies(FFmpeg::avcodec ffmpeg_build)
add_dependencies(FFmpeg::avformat ffmpeg_build)
add_dependencies(FFmpeg::avutil ffmpeg_build)
add_dependencies(FFmpeg::swresample ffmpeg_build)

# Export variables for parent CMakeLists.txt (include dirs passed separately)
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INSTALL_DIR}/include PARENT_SCOPE)
set(FFMPEG_LIBRARIES
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
    PARENT_SCOPE
)

message(STATUS "FFmpeg will be built statically")
message(STATUS "========================================")

# PortAudio

message(STATUS "=== PortAudio Static Build Configuration ===")
set(PA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/portaudio)

if(NOT EXISTS "${PA_SOURCE_DIR}/CMakeLists.txt")
    message(WARNING "PortAudio source not found at ${PA_SOURCE_DIR}")
else()
    set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    
    if(WIN32)
        set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
        set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
        set(PA_USE_DS OFF CACHE BOOL "" FORCE)
        set(PA_USE_WMME OFF CACHE BOOL "" FORCE)
        set(PA_USE_ASIO OFF CACHE BOOL "" FORCE)
    else()
        set(PA_USE_ALSA ON CACHE BOOL "" FORCE)
        set(PA_USE_JACK OFF CACHE BOOL "" FORCE)
        set(PA_USE_PULSEAUDIO OFF CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(portaudio)
    
    # Export variables - Detect correct target name
    if(TARGET portaudio_static)
        set(PORTAUDIO_LIBRARIES portaudio_static PARENT_SCOPE)
    elseif(TARGET portaudio)
        set(PORTAUDIO_LIBRARIES portaudio PARENT_SCOPE)
    else()
        message(WARNING "PortAudio target not found!")
    endif()

    set(PORTAUDIO_INCLUDE_DIRS ${PA_SOURCE_DIR}/include PARENT_SCOPE)
    
    message(STATUS "PortAudio configured (WDM-KS + WASAPI enabled)")
endif()
message(STATUS "==========================================")
