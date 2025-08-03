#include <stdio.h>
#include "callback.h"

int quic_stream_callback(){
    printf("[더미] stream 이벤트 수신");
    return 0;
}