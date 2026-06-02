cmake_minimum_required(VERSION 3.10)
project(BOOK_TO_GAME LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory for external headers like json.hpp and json_fwd.hpp.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/external/vc/include")

# pugixml (XML parser for fb2/epub/docx)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/src/pugixml)

# miniz (ZIP decompressor for epub/docx)
# Built as 'book_miniz' to avoid target name clash with libmobi's internal 'miniz' OBJECT lib.
# Generate miniz_export.h (normally produced by CMake's GenerateExportHeader) at configure time.
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/external/src/miniz/miniz_export.h"
"/* miniz_export.h - generated for static build */\n"
"#ifndef MINIZ_EXPORT_H\n"
"#define MINIZ_EXPORT_H\n"
"#define MINIZ_EXPORT\n"
"#define MINIZ_NO_EXPORT\n"
"#define MINIZ_DEPRECATED\n"
"#define MINIZ_DEPRECATED_EXPORT\n"
"#define MINIZ_DEPRECATED_NO_EXPORT\n"
"#endif /* MINIZ_EXPORT_H */\n"
)
add_library(book_miniz STATIC
    external/src/miniz/miniz.c
    external/src/miniz/miniz_tdef.c
    external/src/miniz/miniz_tinfl.c
    external/src/miniz/miniz_zip.c
)
target_include_directories(book_miniz PUBLIC external/src/miniz)

# 1. EMSCRIPTEN (WebAssembly) BUILD
if(EMSCRIPTEN)
    message(STATUS "Configuring build for Emscripten (WebAssembly)...")
    
    add_executable(${PROJECT_NAME} src/main.cpp src/book_converter.cpp)
    
    # Set the target extension for running in a web browser
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
    
    # Add compiler options for WebAssembly
    target_compile_options(${PROJECT_NAME} PRIVATE
        "-sUSE_SDL=2"
        "-sUSE_SDL_IMAGE=2"
        "-sUSE_SDL_MIXER=2"
        "-sUSE_SDL_TTF=2"
        "-sUSE_SDL_NET=2"
        "-sASYNCIFY=1"
    )
    
    file(GLOB NBOOK_FILES "${CMAKE_CURRENT_SOURCE_DIR}/nbook_*.txt")
    set(NBOOK_PRELOADS "")
    foreach(NBOOK_PATH ${NBOOK_FILES})
        get_filename_component(NBOOK_NAME ${NBOOK_PATH} NAME)
        list(APPEND NBOOK_PRELOADS "--preload-file=${NBOOK_PATH}@${NBOOK_NAME}")
    endforeach()

    # Emscripten port linking and asset preloading flags
    target_link_options(${PROJECT_NAME} PRIVATE
        "-sUSE_SDL=2"
        "-sUSE_SDL_IMAGE=2"
        "-sUSE_SDL_MIXER=2"
        "-sUSE_SDL_TTF=2"
        "-sUSE_SDL_NET=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=67108864"
        "-sASYNCIFY=1"
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/platforms/emscripten/shell.html"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@assets"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/book.json@book.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/settings.json@settings.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/options.json@options.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_gemini.json@ai_gemini.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_puter.json@ai_puter.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_chatgpt.json@ai_chatgpt.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_copilot.json@ai_copilot.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_deepseek.json@ai_deepseek.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_groq.json@ai_groq.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_llama.json@ai_llama.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_lmstudio.json@ai_lmstudio.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_openai.json@ai_openai.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/ai_openrouter.json@ai_openrouter.json"
        ${NBOOK_PRELOADS}
    )
    
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pugixml
        book_miniz
    )
    
    target_include_directories(${PROJECT_NAME} PRIVATE
        external/src/pugixml/src
        external/src/miniz
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

# Linux: Wayland support (dynamic loading = graceful X11 fallback when no Wayland compositor is present, e.g. WSL/WSLg)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(SDL_WAYLAND ON CACHE BOOL "Enable Wayland video driver" FORCE)
    set(SDL_WAYLAND_SHARED ON CACHE BOOL "Dynamically load Wayland support (allows X11 fallback)" FORCE)
    set(SDL_WAYLAND_LIBDECOR ON CACHE BOOL "Use client-side window decorations on Wayland" FORCE)
    set(SDL_WAYLAND_LIBDECOR_SHARED ON CACHE BOOL "Dynamically load libdecor" FORCE)
    set(SDL_X11 ON CACHE BOOL "Enable X11 video driver (fallback)" FORCE)
    set(SDL_X11_SHARED ON CACHE BOOL "Dynamically load X11 support" FORCE)
endif()

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
set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "")
set(CURL_USE_LIBSSH OFF CACHE INTERNAL "")

if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE INTERNAL "")
elseif(IOS OR APPLE)
    set(CURL_USE_MBEDTLS ON CACHE INTERNAL "")
elseif(ANDROID)
    set(CURL_USE_MBEDTLS ON CACHE INTERNAL "")
else()
    set(CURL_USE_MBEDTLS ON CACHE INTERNAL "")
endif()


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
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/src/mbedtls)
set(CURL_DISABLE_INSTALL ON CACHE INTERNAL "")
add_subdirectory(external/src/curl)

# Define the USE_WEBVIEW build option
option(USE_WEBVIEW "Build the native application inside a multiplatform WebView container to support Puter sessions keylessly" OFF)

if(USE_WEBVIEW)
    message(STATUS "Configuring BOOK_TO_GAME target to build as a native WebView2/WebKit container (USE_WEBVIEW=ON)...")
    
    # 1. Select the platform-specific WebView sources
    if(WIN32)
        include(FetchContent)
        FetchContent_Declare(
            webview2_sdk
            URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1264.42"
        )
        FetchContent_MakeAvailable(webview2_sdk)
        include_directories("${webview2_sdk_SOURCE_DIR}/build/native/include")
        
        set(WEBVIEW_SOURCES src/platforms/windows/webview_win32.cpp)
        set(WEBVIEW_LIBS "Shlwapi.lib" "version.lib")
        
    elseif(APPLE AND NOT IOS)
        enable_language(OBJC OBJCXX)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
        find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
        find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
        
        set(WEBVIEW_SOURCES src/platforms/macos/webview_macos.mm)
        set(WEBVIEW_LIBS ${WEBKIT_FRAMEWORK} ${COCOA_FRAMEWORK})
        
    elseif(ANDROID)
        set(WEBVIEW_SOURCES src/platforms/android/webview_jni.cpp)
        set(WEBVIEW_LIBS "android" "log")
        
    elseif(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        pkg_check_modules(WEBKIT2REQUIRED webkit2gtk-4.0)
        include_directories(${GTK3_INCLUDE_DIRS} ${WEBKIT2REQUIRED_INCLUDE_DIRS})
        link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT2REQUIRED_LIBRARY_DIRS})
        
        set(WEBVIEW_SOURCES src/platforms/linux/webview_linux.cpp)
        set(WEBVIEW_LIBS ${GTK3_LIBRARIES} ${WEBKIT2REQUIRED_LIBRARIES})
    endif()

    # 2. Add target with selected WebView sources
    if(ANDROID)
        set(TARGET_NAME main)
        add_library(${TARGET_NAME} SHARED ${WEBVIEW_SOURCES})
    else()
        set(TARGET_NAME ${PROJECT_NAME})
        if(WIN32)
            add_executable(${TARGET_NAME} WIN32 ${WEBVIEW_SOURCES})
        elseif(APPLE AND NOT IOS)
            add_executable(${TARGET_NAME} MACOSX_BUNDLE ${WEBVIEW_SOURCES})
        else()
            add_executable(${TARGET_NAME} ${WEBVIEW_SOURCES})
        endif()
    endif()
    
    target_link_libraries(${TARGET_NAME} PRIVATE ${WEBVIEW_LIBS})

    # Copy the Emscripten/WASM build files to the target execution directory so they can be loaded by the WebView
    if(NOT ANDROID)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/build_web/BOOK_TO_GAME.html" $<TARGET_FILE_DIR:${TARGET_NAME}>/BOOK_TO_GAME.html
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/build_web/BOOK_TO_GAME.js" $<TARGET_FILE_DIR:${TARGET_NAME}>/BOOK_TO_GAME.js
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/build_web/BOOK_TO_GAME.wasm" $<TARGET_FILE_DIR:${TARGET_NAME}>/BOOK_TO_GAME.wasm
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/build_web/BOOK_TO_GAME.data" $<TARGET_FILE_DIR:${TARGET_NAME}>/BOOK_TO_GAME.data
        )
        
        # Copy Microsoft WebView2Loader.dll to the target executable folder on Windows
        if(WIN32)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(WEBVIEW2_LOADER_DLL "${webview2_sdk_SOURCE_DIR}/build/native/x64/WebView2Loader.dll")
            else()
                set(WEBVIEW2_LOADER_DLL "${webview2_sdk_SOURCE_DIR}/build/native/x86/WebView2Loader.dll")
            endif()
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WEBVIEW2_LOADER_DLL}"
                $<TARGET_FILE_DIR:${TARGET_NAME}>/WebView2Loader.dll
            )
        endif()
        
        install(TARGETS ${TARGET_NAME} DESTINATION bin)
    endif()

else()
    # Traditional C++ SDL2 source code target build
    message(STATUS "Configuring traditional C++ SDL2 target build (USE_WEBVIEW=OFF)...")
    
    if(ANDROID)
        set(TARGET_NAME main)
        add_library(${TARGET_NAME} SHARED src/main.cpp src/book_converter.cpp)
    else()
        set(TARGET_NAME ${PROJECT_NAME})
        if(WIN32)
            add_executable(${TARGET_NAME} WIN32 src/main.cpp src/book_converter.cpp)
        elseif(APPLE AND NOT IOS)
            add_executable(${TARGET_NAME} MACOSX_BUNDLE src/main.cpp src/book_converter.cpp)
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
        mbedtls
        mbedx509
        mbedcrypto
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

    # Copy assets and configuration files to application output directory
    if(NOT ANDROID)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            $<TARGET_FILE_DIR:${TARGET_NAME}>/assets
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/book.json"
            $<TARGET_FILE_DIR:${TARGET_NAME}>/book.json
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/settings.json"
            $<TARGET_FILE_DIR:${TARGET_NAME}>/settings.json
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/options.json"
            $<TARGET_FILE_DIR:${TARGET_NAME}>/options.json
        )

        file(GLOB AI_JSON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ai_*.json")
        foreach(AI_FILE ${AI_JSON_FILES})
            get_filename_component(AI_NAME ${AI_FILE} NAME)
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AI_FILE}" $<TARGET_FILE_DIR:${TARGET_NAME}>/${AI_NAME}
            )
        endforeach()

        file(GLOB NBOOK_TXT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/nbook_*.txt")
        foreach(NBOOK_FILE ${NBOOK_TXT_FILES})
            get_filename_component(NBOOK_NAME ${NBOOK_FILE} NAME)
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NBOOK_FILE}" $<TARGET_FILE_DIR:${TARGET_NAME}>/${NBOOK_NAME}
            )
        endforeach()

        install(TARGETS ${TARGET_NAME} DESTINATION bin)
    endif()
endif()

# 4. Standalone Book Summarizer utility (Windows/Desktop Console Application)
if(WIN32)
    add_executable(book_summarizer src/book_summarizer.cpp src/book_converter.cpp)
    target_compile_definitions(book_summarizer PRIVATE CURL_STATICLIB)
    if(MOBI_SUPPORT)
        target_compile_definitions(book_summarizer PRIVATE MOBI_SUPPORT)
    endif()
    
    if(WIN32 AND NOT MSVC)
        target_link_options(book_summarizer PRIVATE "-static-libgcc" "-static-libstdc++")
    endif()
    
    target_link_libraries(book_summarizer PRIVATE
        libcurl
        mbedtls
        mbedx509
        mbedcrypto
        pugixml
        book_miniz
    )
    
    if(MOBI_SUPPORT)
        target_link_libraries(book_summarizer PRIVATE mobi)
    endif()
    
    target_include_directories(book_summarizer PRIVATE
        external/src/SDL2/include
        external/src/pugixml/src
        external/src/miniz
    )
    
    if(MOBI_SUPPORT)
        target_include_directories(book_summarizer PRIVATE
            external/src/libmobi/src
        )
    endif()
endif()

# ==============================================================================
# MULTIPLATFORM NATIVE WEBVIEW WRAPPER BUILD SYSTEM
# ==============================================================================

# Enable Objective-C/Objective-C++ support when compiling for macOS
if(APPLE AND NOT IOS)
    enable_language(OBJC OBJCXX)
endif()

# Platform-specific WebView dependencies and build options
if(WIN32)
    message(STATUS "Configuring Windows WebView2 wrapper build...")
    
    # Locate or configure Microsoft WebView2 SDK via FetchContent
    include(FetchContent)
    FetchContent_Declare(
        webview2_sdk
        URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1264.42"
    )
    FetchContent_MakeAvailable(webview2_sdk)
    
    # Include headers from the downloaded NuGet package
    include_directories("${webview2_sdk_SOURCE_DIR}/build/native/include")
    
    # Source files for Windows WebView
    set(WEBVIEW_SOURCES src/platforms/windows/webview_win32.cpp)
    set(WEBVIEW_LIBS "Shlwapi.lib" "version.lib")
    
elseif(APPLE AND NOT IOS)
    message(STATUS "Configuring macOS WKWebView bundle...")
    
    # Configure Universal Binary Compilation (M1/M2 Apple Silicon & Intel)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    
    # Enable WebKit and Cocoa frameworks
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    
    set(WEBVIEW_SOURCES src/platforms/macos/webview_macos.mm)
    set(WEBVIEW_LIBS ${WEBKIT_FRAMEWORK} ${COCOA_FRAMEWORK})
    
elseif(ANDROID)
    message(STATUS "Configuring Android Native WebView NDK link...")
    
    set(WEBVIEW_SOURCES src/platforms/android/webview_jni.cpp)
    set(WEBVIEW_LIBS "android" "log")

elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring Linux WebKitGTK (webkit2gtk-4.0)...")
    
    # Locate GTK3 and WebKitGTK development packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT2REQUIRED webkit2gtk-4.0)
    
    include_directories(${GTK3_INCLUDE_DIRS} ${WEBKIT2REQUIRED_INCLUDE_DIRS})
    link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT2REQUIRED_LIBRARY_DIRS})
    
    set(WEBVIEW_SOURCES src/platforms/linux/webview_linux.cpp)
    set(WEBVIEW_LIBS ${GTK3_LIBRARIES} ${WEBKIT2REQUIRED_LIBRARIES})
endif()

if(WEBVIEW_SOURCES)
    add_executable(NativeWebViewWrapper ${WEBVIEW_SOURCES})
    target_link_libraries(NativeWebViewWrapper PRIVATE ${WEBVIEW_LIBS})
endif()

# ==============================================================================
# PUTER BRIDGE BUILD TARGET (Keyless Puter AI helper for Windows & Linux)
# ==============================================================================
if(WIN32)
    message(STATUS "Configuring PuterBridge (Windows invisible WebView2 helper)...")
    
    # Locate or configure Microsoft WebView2 SDK via FetchContent
    include(FetchContent)
    FetchContent_Declare(
        webview2_sdk_bridge
        URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1264.42"
    )
    FetchContent_MakeAvailable(webview2_sdk_bridge)
    include_directories("${webview2_sdk_bridge_SOURCE_DIR}/build/native/include")
    
    add_executable(PuterBridge src/platforms/windows/puter_bridge_win32.cpp)
    target_link_libraries(PuterBridge PRIVATE "Shlwapi.lib" "version.lib")
    
    # Copy Microsoft WebView2Loader.dll and puter_bridge.html to output folder
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WEBVIEW2_LOADER_DLL_BRIDGE "${webview2_sdk_bridge_SOURCE_DIR}/build/native/x64/WebView2Loader.dll")
    else()
        set(WEBVIEW2_LOADER_DLL_BRIDGE "${webview2_sdk_bridge_SOURCE_DIR}/build/native/x86/WebView2Loader.dll")
    endif()
    
    add_custom_command(TARGET PuterBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_LOADER_DLL_BRIDGE}"
        $<TARGET_FILE_DIR:PuterBridge>/WebView2Loader.dll
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:PuterBridge>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/puter_bridge.html"
        $<TARGET_FILE_DIR:PuterBridge>/assets/puter_bridge.html
    )
    
    install(TARGETS PuterBridge DESTINATION bin)
    
    # Add dependency so PuterBridge builds automatically with BOOK_TO_GAME
    if(TARGET BOOK_TO_GAME)
        add_dependencies(BOOK_TO_GAME PuterBridge)
    endif()
elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring PuterBridge (Linux invisible WebKitGTK helper)...")
    
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT2REQUIRED webkit2gtk-4.0)
    
    add_executable(PuterBridge src/platforms/linux/puter_bridge_linux.cpp)
    target_link_libraries(PuterBridge PRIVATE ${GTK3_LIBRARIES} ${WEBKIT2REQUIRED_LIBRARIES})
    target_include_directories(PuterBridge PRIVATE ${GTK3_INCLUDE_DIRS} ${WEBKIT2REQUIRED_INCLUDE_DIRS})
    
    # Copy puter_bridge.html to output folder
    add_custom_command(TARGET PuterBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:PuterBridge>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/puter_bridge.html"
        $<TARGET_FILE_DIR:PuterBridge>/assets/puter_bridge.html
    )
    
    install(TARGETS PuterBridge DESTINATION bin)
    
    # Add dependency so PuterBridge builds automatically with BOOK_TO_GAME
    if(TARGET BOOK_TO_GAME)
        add_dependencies(BOOK_TO_GAME PuterBridge)
        
        # Add target to bundle the compiled Linux build into a single self-extracting BookToGame.run file
        add_custom_target(PackLinuxRun
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/pack_run.sh" "$<TARGET_FILE_DIR:BOOK_TO_GAME>"
            DEPENDS BOOK_TO_GAME PuterBridge NativeWebViewWrapper
            COMMENT "Creating single-file self-extracting Linux executable..."
        )
    endif()
endif()