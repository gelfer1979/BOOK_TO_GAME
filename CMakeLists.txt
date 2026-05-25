cmake_minimum_required(VERSION 3.10)
project(BOOK_TO_GAME LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory for external headers like json.hpp and json_fwd.hpp.
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

# 2. STATIC BUILD FROM SOURCE (All Platforms: Windows, Linux, macOS, Android, iOS)
message(STATUS "Configuring static/hardlinked build from source for all platforms...")

# Disable target installation to prevent redundant file copying
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Disable SDL2 install" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries only" FORCE)
set(SDL_SHARED OFF CACHE BOOL "Disable SDL shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Enable SDL static library" FORCE)

# SDL2_image settings
set(SDL2IMAGE_VENDORED ON CACHE BOOL "Use vendored libraries" FORCE)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "Use STB image" FORCE)
set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "Disable shared dependencies" FORCE)

# SDL2_mixer settings
set(SDL2MIXER_VENDORED ON CACHE BOOL "Use vendored libraries" FORCE)
set(SDL2MIXER_OPUS OFF CACHE BOOL "Disable Opus" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "Disable FLAC" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "Disable MOD" FORCE)
set(SDL2MIXER_MP3 OFF CACHE BOOL "Disable MP3" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "Disable MIDI" FORCE)
set(SDL2MIXER_VORBIS "STB" CACHE STRING "Use STB Vorbis" FORCE)
set(SDL2MIXER_WAVPACK OFF CACHE BOOL "Disable WavPack" FORCE)
set(SDL2MIXER_GME OFF CACHE BOOL "Disable GME" FORCE)

# SDL2_ttf settings
set(SDL2TTF_VENDORED ON CACHE BOOL "Use vendored libraries" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "Disable HarfBuzz" FORCE)

# SDL2_net settings
set(SDL2_NET_DISABLE_INSTALL ON CACHE BOOL "Disable Net install" FORCE)

# Configure curl settings (build from source)
set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_TESTING OFF CACHE INTERNAL "")
set(CURL_DISABLE_TESTS ON CACHE INTERNAL "")
set(HTTP_ONLY ON CACHE INTERNAL "")

if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE INTERNAL "")
elseif(IOS OR APPLE)
    set(CURL_USE_SECTRANSP ON CACHE INTERNAL "")
else()
    set(CURL_USE_OPENSSL OFF CACHE INTERNAL "")
endif()

# pugixml (XML parser for fb2/epub/docx)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/src/pugixml)

# miniz (ZIP decompressor for epub/docx)
# Built as 'book_miniz' to avoid target name clash with libmobi's internal 'miniz' OBJECT lib.
# MINIZ_NO_EXPORT bypasses the need for generated miniz_export.h.
add_library(book_miniz STATIC
    external/src/miniz/miniz.c
    external/src/miniz/miniz_tdef.c
    external/src/miniz/miniz_tinfl.c
    external/src/miniz/miniz_zip.c
)
target_include_directories(book_miniz PUBLIC external/src/miniz)

# libmobi (MOBI/AZW reader) — skip on Android/iOS/Emscripten
# libmobi will use its OWN bundled miniz (USE_MINIZ=ON) to avoid target conflicts
set(MOBI_SUPPORT OFF)
if(NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
    set(USE_ENCRYPTION OFF CACHE BOOL "" FORCE)
    set(USE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
    set(USE_MINIZ ON CACHE BOOL "" FORCE)
    set(USE_XMLWRITER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(TOOLS_STATIC OFF CACHE BOOL "" FORCE)
    set(LIBMOBI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/src/libmobi" CACHE PATH "" FORCE)
    add_subdirectory(external/src/libmobi EXCLUDE_FROM_ALL)
    set(MOBI_SUPPORT ON)
endif()

# Add library sources via add_subdirectory
add_subdirectory(external/src/SDL2)

# If shared SDL2 is disabled, sub-libraries seeking target "SDL2" must use "SDL2-static"
if(NOT TARGET SDL2 AND TARGET SDL2-static)
    add_library(SDL2 ALIAS SDL2-static)
endif()

add_subdirectory(external/src/SDL2_image)
add_subdirectory(external/src/SDL2_mixer)
add_subdirectory(external/src/SDL2_ttf)
add_subdirectory(external/src/SDL2_net)
add_subdirectory(external/src/curl)

if(ANDROID)
    set(TARGET_NAME main)
    add_library(${TARGET_NAME} SHARED src/main.cpp src/book_converter.cpp)
else()
    set(TARGET_NAME ${PROJECT_NAME})
    if(WIN32)
        add_executable(${TARGET_NAME} WIN32 src/main.cpp src/book_converter.cpp)
    else()
        add_executable(${TARGET_NAME} src/main.cpp src/book_converter.cpp)
    endif()
endif()

# Define CURL_STATICLIB so that the project consumes the static libcurl correctly
target_compile_definitions(${TARGET_NAME} PRIVATE CURL_STATICLIB)

# Enable MOBI support flag for book_converter.h when libmobi is compiled in
if(MOBI_SUPPORT)
    target_compile_definitions(${TARGET_NAME} PRIVATE MOBI_SUPPORT)
endif()

# Statically link toolchain runtimes where supported (e.g. MinGW)
if(WIN32 AND NOT MSVC)
    target_link_options(${TARGET_NAME} PRIVATE "-static-libgcc" "-static-libstdc++")
endif()

# Link static targets
target_link_libraries(${TARGET_NAME} PRIVATE
    SDL2
    SDL2_image
    SDL2_mixer
    SDL2_ttf
    SDL2_net
    libcurl
    pugixml
    book_miniz
)

if(MOBI_SUPPORT)
    target_link_libraries(${TARGET_NAME} PRIVATE mobi)
endif()

if(ANDROID)
    target_link_libraries(${TARGET_NAME} PRIVATE log android)
endif()

if(TARGET SDL2main)
    target_link_libraries(${TARGET_NAME} PRIVATE SDL2main)
endif()

# Header files search paths for libraries compiled from source
target_include_directories(${TARGET_NAME} PRIVATE
    external/src/SDL2/include
    external/src/SDL2_image
    external/src/SDL2_mixer
    external/src/SDL2_ttf
    external/src/SDL2_net
    external/src/pugixml/src
    external/src/miniz
)

if(MOBI_SUPPORT)
    target_include_directories(${TARGET_NAME} PRIVATE
        external/src/libmobi/src
    )
endif()

# 3. GENERAL: Copy assets folder to application output directory
if(NOT ANDROID)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        $<TARGET_FILE_DIR:${TARGET_NAME}>/assets
    )

    install(TARGETS ${TARGET_NAME} DESTINATION bin)
endif()