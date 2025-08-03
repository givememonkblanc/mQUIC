#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 로그 파일을 초기화합니다.
 * @param filename 로그 파일의 경로.
 * @return 성공 시 0, 실패 시 -1.
 */
int log_init(const char *filename);

/**
 * @brief 로그 파일에 메시지를 씁니다. printf와 사용법이 같습니다.
 * @param format 포맷 문자열.
 * @param ... 가변 인자.
 */
void log_write(const char *format, ...);

/**
 * @brief 로그 파일을 닫습니다.
 */
void log_close();

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H