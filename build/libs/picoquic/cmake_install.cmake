# Install script for directory: /Users/dohyeonlim/mQUIC/libs/picoquic

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
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
  set(CMAKE_OBJDUMP "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/bin/picoquicdemo")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/bin" TYPE EXECUTABLE FILES "/Users/dohyeonlim/mQUIC/build/libs/picoquic/picoquicdemo")
  if(EXISTS "$ENV{DESTDIR}/usr/local/bin/picoquicdemo" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/bin/picoquicdemo")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/Users/dohyeonlim/mQUIC/build/_deps/picotls-src/("
      -delete_rpath "/opt/homebrew/Cellar/brotli/1.1.0/lib"
      -delete_rpath "/Users/dohyeonlim/mQUIC/build/_deps/picotls-src/)"
      "$ENV{DESTDIR}/usr/local/bin/picoquicdemo")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/strip" -u -r "$ENV{DESTDIR}/usr/local/bin/picoquicdemo")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/libpicohttp-core.a")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib" TYPE STATIC_LIBRARY FILES "/Users/dohyeonlim/mQUIC/build/libs/picoquic/libpicohttp-core.a")
  if(EXISTS "$ENV{DESTDIR}/usr/local/lib/libpicohttp-core.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/lib/libpicohttp-core.a")
    execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" "$ENV{DESTDIR}/usr/local/lib/libpicohttp-core.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/include/h3zero.h;/usr/local/include/h3zero_common.h;/usr/local/include/h3zero_uri.h;/usr/local/include/democlient.h;/usr/local/include/demoserver.h;/usr/local/include/pico_webtransport.h;/usr/local/include/wt_baton.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/include" TYPE FILE FILES
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/h3zero.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/h3zero_common.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/h3zero_uri.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/democlient.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/demoserver.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/pico_webtransport.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picohttp/wt_baton.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/bin/picolog_t")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/bin" TYPE EXECUTABLE FILES "/Users/dohyeonlim/mQUIC/build/libs/picoquic/picolog_t")
  if(EXISTS "$ENV{DESTDIR}/usr/local/bin/picolog_t" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/bin/picolog_t")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/Users/dohyeonlim/mQUIC/build/_deps/picotls-src/("
      -delete_rpath "/opt/homebrew/Cellar/brotli/1.1.0/lib"
      -delete_rpath "/Users/dohyeonlim/mQUIC/build/_deps/picotls-src/)"
      "$ENV{DESTDIR}/usr/local/bin/picolog_t")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/strip" -u -r "$ENV{DESTDIR}/usr/local/bin/picolog_t")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/libpicoquic-log.a")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib" TYPE STATIC_LIBRARY FILES "/Users/dohyeonlim/mQUIC/build/libs/picoquic/libpicoquic-log.a")
  if(EXISTS "$ENV{DESTDIR}/usr/local/lib/libpicoquic-log.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/lib/libpicoquic-log.a")
    execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" "$ENV{DESTDIR}/usr/local/lib/libpicoquic-log.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/include/autoqlog.h;/usr/local/include/auto_memlog.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/include" TYPE FILE FILES
    "/Users/dohyeonlim/mQUIC/libs/picoquic/loglib/autoqlog.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/loglib/auto_memlog.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/libpicoquic-core.a")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib" TYPE STATIC_LIBRARY FILES "/Users/dohyeonlim/mQUIC/build/libs/picoquic/libpicoquic-core.a")
  if(EXISTS "$ENV{DESTDIR}/usr/local/lib/libpicoquic-core.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/lib/libpicoquic-core.a")
    execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" "$ENV{DESTDIR}/usr/local/lib/libpicoquic-core.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/libpicotls-core.a")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib" TYPE STATIC_LIBRARY FILES "/Users/dohyeonlim/mQUIC/build/_deps/picotls-build/libpicotls-core.a")
  if(EXISTS "$ENV{DESTDIR}/usr/local/lib/libpicotls-core.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/lib/libpicotls-core.a")
    execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" "$ENV{DESTDIR}/usr/local/lib/libpicotls-core.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/libpicotls-openssl.a")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib" TYPE STATIC_LIBRARY FILES "/Users/dohyeonlim/mQUIC/build/_deps/picotls-build/libpicotls-openssl.a")
  if(EXISTS "$ENV{DESTDIR}/usr/local/lib/libpicotls-openssl.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/lib/libpicotls-openssl.a")
    execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" "$ENV{DESTDIR}/usr/local/lib/libpicotls-openssl.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/libpicotls-minicrypto.a")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib" TYPE STATIC_LIBRARY FILES "/Users/dohyeonlim/mQUIC/build/_deps/picotls-build/libpicotls-minicrypto.a")
  if(EXISTS "$ENV{DESTDIR}/usr/local/lib/libpicotls-minicrypto.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/lib/libpicotls-minicrypto.a")
    execute_process(COMMAND "/Users/dohyeonlim/Downloads/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" "$ENV{DESTDIR}/usr/local/lib/libpicotls-minicrypto.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/include/picoquic.h;/usr/local/include/picosocks.h;/usr/local/include/picoquic_utils.h;/usr/local/include/picoquic_packet_loop.h;/usr/local/include/picoquic_unified_log.h;/usr/local/include/picoquic_logger.h;/usr/local/include/picoquic_binlog.h;/usr/local/include/picoquic_config.h;/usr/local/include/picoquic_lb.h;/usr/local/include/picoquic_newreno.h;/usr/local/include/picoquic_cubic.h;/usr/local/include/picoquic_bbr.h;/usr/local/include/picoquic_bbr1.h;/usr/local/include/picoquic_fastcc.h;/usr/local/include/picoquic_prague.h;/usr/local/include/siphash.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/include" TYPE FILE FILES
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picosocks.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_utils.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_packet_loop.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_unified_log.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_logger.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_binlog.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_config.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_lb.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_newreno.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_cubic.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_bbr.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_bbr1.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_fastcc.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/picoquic_prague.h"
    "/Users/dohyeonlim/mQUIC/libs/picoquic/picoquic/siphash.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/lib/cmake/picoquic/picoquic-config.cmake;/usr/local/lib/cmake/picoquic/picoquic-config-version.cmake")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/lib/cmake/picoquic" TYPE FILE FILES
    "/Users/dohyeonlim/mQUIC/build/libs/picoquic/picoquic-config.cmake"
    "/Users/dohyeonlim/mQUIC/build/libs/picoquic/picoquic-config-version.cmake"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/Users/dohyeonlim/mQUIC/build/_deps/picotls-build/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/dohyeonlim/mQUIC/build/libs/picoquic/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
