#include "camera.h"
#include <opencv2/opencv.hpp>
#include <vector>

// extern "C" 블록으로 함수들을 감싸서 C 스타일로 컴파일되게 합니다.
extern "C" {

/**
 * @brief 새로운 카메라 객체를 생성하고 그 핸들을 반환합니다.
 */
camera_handle_t camera_create() {
    // cv::VideoCapture 객체를 동적으로 생성합니다.
    cv::VideoCapture* cap = new cv::VideoCapture(0); // 0번 카메라(기본 카메라) 열기

    if (!cap || !cap->isOpened()) {
        fprintf(stderr, "카메라 열기 실패\n");
        delete cap; // 실패 시 할당된 메모리 해제
        return nullptr; // 실패 시 NULL 반환
    }

    // 성공 시 객체의 포인터를 camera_handle_t(void*)로 변환하여 반환
    return static_cast<camera_handle_t>(cap);
}

/**
 * @brief camera_create로 생성된 카메라 객체를 소멸시키고 리소스를 해제합니다.
 */
void camera_destroy(camera_handle_t handle) {
    if (handle == nullptr) {
        return;
    }
    // void* 핸들을 다시 원래의 cv::VideoCapture 포인터로 변환
    cv::VideoCapture* cap = static_cast<cv::VideoCapture*>(handle);
    cap->release(); // 카메라 리소스 해제
    delete cap;     // 동적으로 할당된 메모리 해제
}

/**
 * @brief 특정 카메라에서 프레임을 캡처하여 JPEG 형식으로 인코딩합니다.
 */
int camera_capture_jpeg(camera_handle_t handle, unsigned char* buffer, int buf_size) {
    if (handle == nullptr) {
        fprintf(stderr, "유효하지 않은 카메라 핸들입니다.\n");
        return -1;
    }
    // 핸들을 원래 타입으로 변환
    cv::VideoCapture* cap = static_cast<cv::VideoCapture*>(handle);

    cv::Mat frame;
    if (!cap->read(frame)) {
        fprintf(stderr, "프레임 캡처 실패\n");
        return -2;
    }

    std::vector<uchar> jpg_buf;
    if (!cv::imencode(".jpg", frame, jpg_buf)) {
        fprintf(stderr, "JPEG 인코딩 실패\n");
        return -3;
    }

    if ((int)jpg_buf.size() > buf_size) {
        fprintf(stderr, "버퍼 크기가 부족합니다. 필요한 크기: %zu, 제공된 크기: %d\n", jpg_buf.size(), buf_size);
        return -4;
    }

    // 인코딩된 데이터를 제공된 버퍼로 복사
    memcpy(buffer, jpg_buf.data(), jpg_buf.size());

    // 성공 시 JPEG 데이터의 크기 반환
    return (int)jpg_buf.size();
}

} // extern "C"