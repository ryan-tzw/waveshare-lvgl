#ifndef SIMULATOR_ENGINE_H
#define SIMULATOR_ENGINE_H

#include "simulator_engine_t.h"
#include "socket.h"

typedef struct DIRECTLY_CONNECTED_SIMULATOR_ENGINE {
  TCP_SOCKET *tcp_socket;
  bool connected;
  SIMULATOR_ENGINE *simulator_engine;
} DIRECTLY_CONNECTED_SIMULATOR_ENGINE;

void init_simulator_engine(void);

#endif