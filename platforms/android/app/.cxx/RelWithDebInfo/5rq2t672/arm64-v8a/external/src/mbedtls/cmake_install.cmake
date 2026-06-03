# Install script for directory: C:/games/BOOK_TO_GAME/external/src/mbedtls

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/BOOK_TO_GAME")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "RelWithDebInfo")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/android/sdk/ndk/25.2.9519653/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-objdump.exe")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("C:/games/BOOK_TO_GAME/platforms/android/app/.cxx/RelWithDebInfo/5rq2t672/arm64-v8a/external/src/mbedtls/framework/cmake_install.cmake")
  include("C:/games/BOOK_TO_GAME/platforms/android/app/.cxx/RelWithDebInfo/5rq2t672/arm64-v8a/external/src/mbedtls/include/cmake_install.cmake")
  include("C:/games/BOOK_TO_GAME/platforms/android/app/.cxx/RelWithDebInfo/5rq2t672/arm64-v8a/external/src/mbedtls/3rdparty/cmake_install.cmake")
  include("C:/games/BOOK_TO_GAME/platforms/android/app/.cxx/RelWithDebInfo/5rq2t672/arm64-v8a/external/src/mbedtls/library/cmake_install.cmake")
  include("C:/games/BOOK_TO_GAME/platforms/android/app/.cxx/RelWithDebInfo/5rq2t672/arm64-v8a/external/src/mbedtls/pkgconfig/cmake_install.cmake")

endif()

