#ifndef H1NET_H1_TRANSPORT_H
#define H1NET_H1_TRANSPORT_H
#include "h1net_client.h"
int h1net_transport_open(void);
int h1net_transport_open_attached(void);
void h1net_transport_close(void);
void h1net_transport_poll(void);
int h1net_transport_needs_unplug(void);
int h1net_transport_faulted(void);
int h1net_transport_connected(void);
uint32_t h1net_transport_epoch(void);
void h1net_transport_bind(void);
int h1net_transport_write(void *,const uint8_t *,unsigned);
int h1net_transport_read(void *,uint8_t *,unsigned);
int h1net_transport_finish(void *);
#endif
