#ifndef SIMULATOR_ENGINE_H
#define SIMULATOR_ENGINE_H

#include "socket.h"

typedef struct CONNECTED_SIMULATOR_ENGINE {
  TCP_SOCKET *tcp_socket;
  bool connected;
} CONNECTED_SIMULATOR_ENGINE;

void init_simulator_engine(void);

#endif