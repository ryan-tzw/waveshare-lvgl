#ifndef SIMULATOR_ENGINE_H
#define SIMULATOR_ENGINE_H

#include "pio.h"
#include "protocol.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "crc16.h"
#include "core1_task_queue.h"
#include "socket.h"

typedef struct CONNECTED_SIMULATOR_ENGINE {
    TCP_SOCKET* tcp_socket;
    bool connected;
} CONNECTED_SIMULATOR_ENGINE;

void init_simulator_engine(void);



#endif