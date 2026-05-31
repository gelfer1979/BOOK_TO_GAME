# Install script for directory: C:/games/BOOK_TO_GAME/external/src/mbedtls/include

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
    set(CMAKE_INSTALL_CONFIG_NAME "")
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

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/msys64/ucrt64/bin/objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/aes.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/aria.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/asn1.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/asn1write.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/base64.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/bignum.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/block_cipher.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/build_info.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/camellia.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ccm.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/chacha20.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/chachapoly.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/check_config.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/cipher.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/cmac.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/compat-2.x.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_adjust_legacy_crypto.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_adjust_legacy_from_psa.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_adjust_psa_from_legacy.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_adjust_psa_superset_legacy.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_adjust_ssl.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_adjust_x509.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/config_psa.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/constant_time.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ctr_drbg.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/debug.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/des.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/dhm.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ecdh.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ecdsa.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ecjpake.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ecp.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/entropy.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/error.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/gcm.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/hkdf.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/hmac_drbg.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/lms.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/mbedtls_config.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/md.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/md5.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/memory_buffer_alloc.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/net_sockets.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/nist_kw.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/oid.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/pem.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/pk.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/pkcs12.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/pkcs5.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/pkcs7.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/platform.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/platform_time.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/platform_util.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/poly1305.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/private_access.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/psa_util.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ripemd160.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/rsa.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/sha1.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/sha256.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/sha3.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/sha512.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ssl.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ssl_cache.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ssl_ciphersuites.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ssl_cookie.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/ssl_ticket.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/threading.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/timing.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/version.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/x509.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/x509_crl.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/x509_crt.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/mbedtls/x509_csr.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/psa" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/build_info.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_adjust_auto_enabled.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_adjust_config_key_pair_types.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_adjust_config_synonyms.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_builtin_composites.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_builtin_key_derivation.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_builtin_primitives.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_compat.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_config.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_driver_common.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_driver_contexts_composites.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_driver_contexts_key_derivation.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_driver_contexts_primitives.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_extra.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_legacy.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_platform.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_se_driver.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_sizes.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_struct.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_types.h"
    "C:/games/BOOK_TO_GAME/external/src/mbedtls/include/psa/crypto_values.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/games/BOOK_TO_GAME/build_webview_test2/external/src/mbedtls/include/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
