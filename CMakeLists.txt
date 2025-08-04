cmake_minimum_required(VERSION 3.14)
project(mquic_server C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 1. 외부 라이브러리 찾기
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# 2. picoquic 하위 프로젝트 포함
set(PICOQUIC_FETCH_PTLS YES CACHE BOOL "Enable PTLS fetching")
set(PICOPROBE OFF CACHE BOOL "Disable picoprobe")
add_subdirectory(libs/picoquic)

# 3. include 디렉토리 추가 (qlog.h가 있는 곳 포함)
include_directories(
    include
    libs/picoquic/loglib       # ✅ QLOG 헤더가 있는 위치
    ${OpenCV_INCLUDE_DIRS}
)

# 4. 내 소스파일 정의 (qlog.c도 명시적으로 추가)
set(SOURCES
    main.c
    src/quic_server.c
    src/callback.c
    src/path_algo.c
    src/camera.cpp
    src/logger.c
    libs/picoquic/loglib/qlog.c
    libs/picoquic/loglib/logconvert.c
    libs/picoquic/loglib/logreader.c
    libs/picoquic/loglib/memory_log.c
    libs/picoquic/loglib/cidset.c
    libs/picoquic/loglib/csv.c
    libs/picoquic/loglib/svg.c
    libs/picoquic/loglib/autoqlog.c
)

# 5. 실행 파일 생성
add_executable(mquic_server ${SOURCES})

# 6. 의존성 및 라이브러리 링크
add_dependencies(mquic_server picoquic-core)

target_link_libraries(mquic_server PRIVATE
    picoquic-core
    ${OpenCV_LIBS}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)
