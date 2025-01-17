#ifndef QUIC_MOSQ_H
#define QUIC_MOSQ_H

#ifdef WITH_QUIC
#include "msquic.h"
#include "mosquitto.h"
#include "mosquitto_internal.h"


int msquic_init(void);
void msquic_cleanup(void);

int msquic_config(struct mosquitto *mosq);

int msquic_try_connect(struct mosquitto *mosq, const char *host, uint16_t port, const char *bind_address);
int msquic_try_close(struct mosq_quic_connection *connection);

ssize_t msquic_send(struct mosquitto *mosq, const void *buf, size_t count);

#endif

#endif