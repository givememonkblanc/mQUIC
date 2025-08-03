#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h" // 반드시 자신의 헤더를 포함해야 합니다.

// 로그 파일 포인터를 전역 변수로 관리
static FILE *log_file = NULL;

int log_init(const char *filename) {
    // "a" 모드는 파일이 있으면 뒤에 이어 쓰고, 없으면 새로 만듭니다.
    log_file = fopen(filename, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return -1;
    }
    return 0;
}

void log_write(const char *format, ...) {
    if (log_file == NULL) {
        return; // 로그 파일이 열려있지 않으면 아무것도 하지 않음
    }

    // 현재 시간을 로그에 함께 기록
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(log_file, "[%s] ", time_str);

    // 가변 인자 처리
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file); // 버퍼를 비워 바로 파일에 쓰도록 함
}

void log_close() {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}