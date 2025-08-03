#ifndef CALLBACK_H
#define CALLBACK_H

#ifdef __cplusplus
extern "C" {
#endif

// 필요 시 실제 picoquic의 callback signature에 맞춰 수정하세요
int quic_stream_callback(/* arguments 맞춰주세요 */);

#ifdef __cplusplus
}
#endif

#endif // CALLBACK_H
