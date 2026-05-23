cmake_minimum_required(VERSION 3.14)
project(BOOK_TO_GAME LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory for external headers like json.hpp and json_fwd.hpp.
# This path is specified by the user to contain these headers and is added for all targets.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/external/vc/include")

# 1. EMSCRIPTEN (WebAssembly) BUILD
if(EMSCRIPTEN)
    message(STATUS "Configuring build for Emscripten (WebAssembly)...")
    
    add_executable(${PROJECT_NAME} src/main.cpp)
    
    # Set the target extension for running in a web browser
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
    
    # Add curl compiler options for WebAssembly
    target_compile_options(${PROJECT_NAME} PRIVATE "-s" "USE_CURL=1")
    
    # Emscripten port linking and asset preloading flags
    target_link_options(${PROJECT_NAME} PRIVATE
        "-s" "USE_SDL=2"
        "-s" "USE_SDL_IMAGE=2"
        "-s" "USE_SDL_MIXER=2"
        "-s" "USE_SDL_TTF=2"
        "-s" "USE_SDL_NET=2"
        "-s" "USE_CURL=1"
        "-s" "ALLOW_MEMORY_GROWTH=1"
        "-s" "INITIAL_MEMORY=67108864"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets@assets"
    )
    return()
endif()

# 2. MOBILE BUILD (Android / iOS) FROM SOURCE
if(ANDROID OR IOS)
    message(STATUS "Configuring mobile build for Android/iOS from source...")
    
    # Disable target installation to prevent redundant file copying
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Disable SDL2 install" FORCE)
    
    # SDL2_image settings
    set(SUPPORT_PNG ON CACHE BOOL "Support PNG" FORCE)
    set(SUPPORT_JPG ON CACHE BOOL "Support JPG" FORCE)
    set(SUPPORT_WEBP OFF CACHE BOOL "Support WEBP" FORCE)
    set(SDL2_IMAGE_BACKEND_STB ON CACHE BOOL "Use STB" FORCE)
    
    # SDL2_mixer settings
    set(SDL2_MIXER_DISABLE_INSTALL ON CACHE BOOL "Disable Mixer install" FORCE)
    set(SUPPORT_WAV ON CACHE BOOL "Support WAV" FORCE)
    set(SUPPORT_MOD OFF CACHE BOOL "Support MOD" FORCE)
    set(SUPPORT_MIDI OFF CACHE BOOL "Support MIDI" FORCE)
    set(SUPPORT_OGG OFF CACHE BOOL "Support OGG" FORCE)
    set(SUPPORT_MP3 OFF CACHE BOOL "Support MP3" FORCE)
    
    # SDL2_ttf settings
    set(SDL2_TTF_DISABLE_INSTALL ON CACHE BOOL "Disable TTF install" FORCE)
    set(SDL2_TTF_HARFBUZZ OFF CACHE BOOL "Use HarfBuzz" FORCE)
    
    # SDL2_net settings
    set(SDL2_NET_DISABLE_INSTALL ON CACHE BOOL "Disable Net install" FORCE)
    
    # Configure curl settings for mobile (build from source)
    set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    set(BUILD_TESTING OFF CACHE INTERNAL "")
    set(CURL_DISABLE_TESTS ON CACHE INTERNAL "")
    set(HTTP_ONLY ON CACHE INTERNAL "")
    if(IOS)
        set(CURL_USE_SECTRANSP ON CACHE INTERNAL "")
    else()
        set(CURL_USE_OPENSSL OFF CACHE INTERNAL "")
    endif()
    
    # Add library sources via add_subdirectory
    add_subdirectory(external/src/SDL2)
    add_subdirectory(external/src/SDL2_image)
    add_subdirectory(external/src/SDL2_mixer)
    add_subdirectory(external/src/SDL2_ttf)
    add_subdirectory(external/src/SDL2_net)
    add_subdirectory(external/src/curl)
    
    add_executable(${PROJECT_NAME} src/main.cpp)
    
    # Link static/dynamic target versions of built libraries and libcurl
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2main
        SDL2
        SDL2_image
        SDL2_mixer
        SDL2_ttf
        SDL2_net
        libcurl
    )
    
    # On mobile platforms, SDL2 header files are located in root source directories
    target_include_directories(${PROJECT_NAME} PRIVATE
        external/src/SDL2/include
        external/src/SDL2_image
        external/src/SDL2_mixer
        external/src/SDL2_ttf
        external/src/SDL2_net
    )
    return()
endif()

# 3. DESKTOP BUILD (Windows / Linux)
if(WIN32)
    message(STATUS "Configuring desktop build for Windows...")
    
    # Determine CPU target architecture (x64 / x86)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "x64")
    else()
        set(ARCH "x86")
    endif()
    
    # Select precompiled library folder based on compiler family
    if(MSVC)
        set(SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vc")
        message(STATUS "Using MSVC binaries at: ${SDL_DIR}")
    else()
        set(SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/mingw")
        message(STATUS "Using MinGW binaries at: ${SDL_DIR}")
    endif()
    
    # Include search paths
    include_directories("${SDL_DIR}/include")
    link_directories("${SDL_DIR}/lib/${ARCH}")
    
    # Configure curl settings for Windows (build static libcurl from source)
    set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    set(BUILD_TESTING OFF CACHE INTERNAL "")
    set(CURL_DISABLE_TESTS ON CACHE INTERNAL "")
    set(HTTP_ONLY ON CACHE INTERNAL "")
    set(CURL_USE_SCHANNEL ON CACHE INTERNAL "")
    
    # Add curl source subdirectory
    add_subdirectory(external/src/curl)
    
    add_executable(${PROJECT_NAME} src/main.cpp)
    
    # Define CURL_STATICLIB so that the project consumes the static libcurl correctly
    target_compile_definitions(${PROJECT_NAME} PRIVATE CURL_STATICLIB)
    
    # Link statically to the C++ runtime on MinGW builds to avoid dependency on external DLLs,
    # but keep dynamic linking on MSVC to ensure compatibility with the Visual C++ Redistributable  
    if(NOT MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE "-static-libgcc" "-static-libstdc++")
    endif()
    
    # Link SDL2, libcurl, and core Windows Win32 API system libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2main
        SDL2
        SDL2_image
        SDL2_mixer
        SDL2_ttf
        SDL2_net
        libcurl
        shell32
        user32
        gdi32
        winmm
        imm32
        ole32
        oleaut32
        version
        uuid
    )
    
    # Automatically copy required .dll files to the build folder post-compile
    file(GLOB ALL_DLLS "${SDL_DIR}/bin/${ARCH}/*.dll")
    foreach(dll ${ALL_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${dll}"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endforeach()
    
else()
    message(STATUS "Configuring desktop build for macOS/Linux...")
    
    # On Linux/macOS, discover installed system dependencies using find_package
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(SDL2_net REQUIRED)
    find_package(CURL REQUIRED)
    
    add_executable(${PROJECT_NAME} src/main.cpp)
    
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        SDL2_ttf::SDL2_ttf
        SDL2_net::SDL2_net
        CURL::libcurl
    )
endif()

# 4. GENERAL: Copy assets folder to application output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)
