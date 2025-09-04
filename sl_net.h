#ifndef SL_NET_H
#define SL_NET_H

#include <stdint.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdbool.h>

#include "sl_log.h"
#include "sl_arena.h"
#include "sl_fcgi.h"

typedef struct sl_net_connection sl_net_connection;

struct sl_net_connection {
    int socket_fd;
    struct sockaddr_in address;
    bool is_busy;
    sl_arena arena;
    sl_log log;
    sl_fcgi_parser parser;
    sl_fcgi_request request;
};

void sl_net_create_address(struct sockaddr_in *address, uint32_t ip_address, uint16_t port);
int sl_net_create_listen_socket(uint32_t ip_address, uint16_t port, int backlog);
int sl_net_set_nonblocking_socket(int socket_fd);

void sl_net_init_connection(sl_net_connection *connection, sl_log *log, int socket_fd, struct sockaddr_in address, size_t arena_preallocate, size_t param_hashtable_size);
sl_net_connection *sl_net_find_connection(sl_net_connection *connections, size_t max_connections, int socket_fd);
sl_net_connection *sl_net_find_free_connection(sl_net_connection *connections, size_t max_connections);
void sl_net_destroy_connections(sl_net_connection *connections, size_t max_connections);

#endif
