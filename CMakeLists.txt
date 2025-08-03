cmake_minimum_required(VERSION 3.14)
project(mquic_server C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 1. 외부 라이브러리 찾기 (우리 프로젝트가 직접 사용하는 것들)
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# 2. picoquic을 하위 프로젝트로 빌드 설정
#    picoquic이 picotls를 자동으로 다운로드하고 빌드하도록 설정합니다.
set(PICOQUIC_FETCH_PTLS YES CACHE BOOL "Enable PTLS fetching")
set(PICOPROBE OFF CACHE BOOL "Disable picoprobe")
add_subdirectory(libs/picoquic)

# 3. 내 프로젝트 소스 파일 정의
include_directories(include ${OpenCV_INCLUDE_DIRS})
set(SOURCES
    main.c
    src/quic_server.c
    src/callback.c
    src/path_algo.c
    src/camera.cpp
    src/logger.c
)
add_executable(mquic_server ${SOURCES})

# 4. 빌드 순서 지정
#    mquic_server를 만들기 전에 picoquic_static을 먼저 만들도록 지정합니다.
add_dependencies(mquic_server picoquic-core)

# 5. 라이브러리 연결
#    picoquic_static 타겟 하나만 연결하면,
#    CMake가 알아서 picotls, OpenSSL, Threads 등을 모두 가져와 연결해줍니다.
target_link_libraries(mquic_server PRIVATE
     picoquic-core
    ${OpenCV_LIBS}
)