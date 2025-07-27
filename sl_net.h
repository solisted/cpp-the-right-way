#ifndef SL_NET_H
#define SL_NET_H

#include <stdint.h>
#include <unistd.h>
#include <netinet/in.h>

void sl_net_create_address(struct sockaddr_in *address, uint32_t ip_address, uint16_t port);
int sl_net_create_listen_socket(uint32_t ip_address, uint16_t port, int backlog);

#endif
