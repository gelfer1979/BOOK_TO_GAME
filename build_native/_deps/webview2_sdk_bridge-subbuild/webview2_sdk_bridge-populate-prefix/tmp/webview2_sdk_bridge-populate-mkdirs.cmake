# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-src")
  file(MAKE_DIRECTORY "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-src")
endif()
file(MAKE_DIRECTORY
  "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-build"
  "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix"
  "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix/tmp"
  "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix/src/webview2_sdk_bridge-populate-stamp"
  "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix/src"
  "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix/src/webview2_sdk_bridge-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix/src/webview2_sdk_bridge-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/games/BOOK_TO_GAME/build_native/_deps/webview2_sdk_bridge-subbuild/webview2_sdk_bridge-populate-prefix/src/webview2_sdk_bridge-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
