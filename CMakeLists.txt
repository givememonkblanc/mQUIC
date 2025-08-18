cmake_minimum_required(VERSION 3.14)
project(mquic_server_client C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ===== 외부 라이브러리 찾기 =====
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ===== picotls 하위 빌드 옵션을 '미리' 캐시에 주입 (핵심) =====
# OpenSSL 사용 여부 (OpenSSL 없이 빌드하려면 둘 다 OFF로 바꾸세요)
set(PTLS_WITH_OPENSSL   ON  CACHE BOOL "" FORCE)
set(PTLS_USE_OPENSSL    ON  CACHE BOOL "" FORCE)

# 프로그램/테스트/벤치/푸저/CLI 비활성화 → "cli 타깃" 오류 원천 차단
set(PTLS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(PTLS_BUILD_BENCH    OFF CACHE BOOL "" FORCE)
set(PTLS_BUILD_FUZZER   OFF CACHE BOOL "" FORCE)
set(PTLS_BUILD_CLI      OFF CACHE BOOL "" FORCE)
set(PTLS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING       OFF CACHE BOOL "" FORCE)

# OpenSSL 경로 힌트를 하위로 전파
if(DEFINED OPENSSL_ROOT_DIR)
  list(PREPEND CMAKE_PREFIX_PATH "${OPENSSL_ROOT_DIR}")
endif()
# picotls가 참조할 수 있게 include와 lib 경로를 캐시에 밀어넣기
if(DEFINED OPENSSL_INCLUDE_DIR)
  set(PTLS_OPENSSL_INCLUDE_DIR "${OPENSSL_INCLUDE_DIR}" CACHE PATH "" FORCE)
else()
  # OpenSSL::SSL 타깃에서 include 경로를 뽑아 쓸 수 있으면 사용
  if(TARGET OpenSSL::SSL)
    get_target_property(_OSSLC_INCS OpenSSL::SSL INTERFACE_INCLUDE_DIRECTORIES)
    if(_OSSLC_INCS)
      set(PTLS_OPENSSL_INCLUDE_DIR "${_OSSLC_INCS}" CACHE PATH "" FORCE)
    endif()
  endif()
endif()
if(DEFINED OPENSSL_SSL_LIBRARY)
  set(PTLS_OPENSSL_LIBSSL    "${OPENSSL_SSL_LIBRARY}"    CACHE FILEPATH "" FORCE)
endif()
if(DEFINED OPENSSL_CRYPTO_LIBRARY)
  set(PTLS_OPENSSL_LIBCRYPTO "${OPENSSL_CRYPTO_LIBRARY}" CACHE FILEPATH "" FORCE)
endif()

# ===== picoquic 하위 프로젝트 포함 =====
# picoquic이 내부에서 picotls를 FetchContent로 가져올 때, 위 캐시 옵션들이 적용됩니다.
set(PICOQUIC_FETCH_PTLS ON  CACHE BOOL "Enable PTLS fetching" FORCE)
set(PICOPROBE          OFF CACHE BOOL "Disable picoprobe"     FORCE)
# (선택) picoquic 자체 테스트를 끄고 싶다면 주석 해제
# set(PICOQUIC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(libs/picoquic)
# 현재 디렉터리의 링크 디렉터리 목록에서 (...)/( ) 패턴만 제거
get_property(_ldirs DIRECTORY PROPERTY LINK_DIRECTORIES)
if(_ldirs)
  list(FILTER _ldirs EXCLUDE REGEX "/_deps/picotls-src/\\(|/_deps/picotls-src/\\)")
  set_property(DIRECTORY PROPERTY LINK_DIRECTORIES "${_ldirs}")
endif()

# 우리 타깃에도 동일 필터 적용 (안전)
foreach(tgt mquic_client mquic_server)
  if(TARGET ${tgt})
    get_property(_tdirs TARGET ${tgt} PROPERTY LINK_DIRECTORIES)
    if(_tdirs)
      list(FILTER _tdirs EXCLUDE REGEX "/_deps/picotls-src/\\(|/_deps/picotls-src/\\)")
      set_property(TARGET ${tgt} PROPERTY LINK_DIRECTORIES "${_tdirs}")
    endif()
  endif()
endforeach()
# ===== 공통 include 디렉토리 =====
set(COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/picoquic/loglib
    ${OpenCV_INCLUDE_DIRS}
)

# FetchContent로 내려간 picotls 헤더 포함
if(EXISTS "${CMAKE_BINARY_DIR}/_deps/picotls-src/include")
    list(APPEND COMMON_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/_deps/picotls-src/include")
endif()

# ===== 공통 라이브러리 =====
set(COMMON_LIBS
    picoquic-core
    ${OpenCV_LIBS}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# -------------------------------
# 서버 빌드 설정
# -------------------------------
set(SERVER_SOURCES
    main.c
    src/quic_server.c
    src/callback.c
    src/path_algo.c
    src/camera.cpp
    src/logger.c
    src/h3zero.c
    src/h3zero_common.c
    src/h3zero_server_min.c
    src/pico_webtransport.c
    libs/picoquic/loglib/qlog.c
    libs/picoquic/loglib/logconvert.c
    libs/picoquic/loglib/logreader.c
    libs/picoquic/loglib/memory_log.c
    libs/picoquic/loglib/cidset.c
    libs/picoquic/loglib/csv.c
    libs/picoquic/loglib/svg.c
    libs/picoquic/loglib/autoqlog.c
)

add_executable(mquic_server ${SERVER_SOURCES})
target_include_directories(mquic_server PRIVATE ${COMMON_INCLUDE_DIRS})
target_link_libraries(mquic_server PRIVATE ${COMMON_LIBS})

# -------------------------------
# 클라이언트 빌드 설정
# -------------------------------
set(CLIENT_SOURCES
    client/client_main.c
    client/tls_trust.c
    client/wt_callback.c
    client/wt_client.c
    client/h3zero_client.c
    client/h3zero_qpack.c
    src/h3zero.c
    src/h3zero_common.c
    src/pico_webtransport.c
    libs/picoquic/loglib/qlog.c
    libs/picoquic/loglib/logconvert.c
    libs/picoquic/loglib/logreader.c
    libs/picoquic/loglib/memory_log.c
    libs/picoquic/loglib/cidset.c
    libs/picoquic/loglib/csv.c
    libs/picoquic/loglib/svg.c
    libs/picoquic/loglib/autoqlog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/picoquic/picohttp/h3zero_server.c
)

add_executable(mquic_client ${CLIENT_SOURCES})
target_include_directories(mquic_client PRIVATE ${COMMON_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/libs/picoquic/picohttp)
target_link_libraries(mquic_client PRIVATE ${COMMON_LIBS})

# -------------------------------
# 빌드 타입 기본값 지정 (Debug)
# -------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
