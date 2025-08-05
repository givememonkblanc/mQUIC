cmake_minimum_required(VERSION 3.14)
project(mquic_server_client C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ===== 외부 라이브러리 찾기 =====
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ===== picoquic 하위 프로젝트 포함 =====
set(PICOQUIC_FETCH_PTLS YES CACHE BOOL "Enable PTLS fetching")
set(PICOPROBE OFF CACHE BOOL "Disable picoprobe")
add_subdirectory(libs/picoquic)

# ===== 공통 include 디렉토리 =====
set(COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/picoquic/loglib
    ${OpenCV_INCLUDE_DIRS}
)

if(EXISTS "${CMAKE_BINARY_DIR}/_deps/picotls-src/include")
    list(APPEND COMMON_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/_deps/picotls-src/include)
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
    client/frame_client.c         # 우리가 작성한 순수 WT 클라이언트
    client/h3zero_client.c        # CONNECT 요청 생성기
    client/h3zero_qpack.c         # 최소 QPACK literal 구현
    src/h3zero.c                  # H3Zero core (HEADERS parsing 등)
    src/h3zero_common.c           # 공통 유틸
    src/pico_webtransport.c       # WebTransport caps 처리
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
