#include "sl_net.h"

#include <fcntl.h>

#include <string.h>

void sl_net_create_address(struct sockaddr_in *address, uint32_t ip_address, uint16_t port)
{
    memset(address, 0, sizeof(struct sockaddr_in));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(ip_address);
    address->sin_port = htons(port);
}

int sl_net_create_listen_socket(uint32_t ip_address, uint16_t port, int backlog)
{
    int listen_socket;
    struct sockaddr_in address;

    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == -1) {
        return -1;
    }

    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));

    sl_net_create_address(&address, ip_address, port);

    if (bind(listen_socket, (struct sockaddr*) &address,  sizeof(address)) == -1) {
        return -1;
    }

    if (listen(listen_socket, backlog) == -1) {
        return -1;
    }

    return listen_socket;
}

int sl_net_set_nonblocking_socket(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFL);
    if (flags == -1) {
        return -1;
    }

    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
}

void sl_net_init_connection(sl_net_connection *connection, sl_log *log, int socket_fd, struct sockaddr_in address, size_t arena_preallocate, size_t param_hashtable_size)
{
    connection->socket_fd = socket_fd;
    connection->address = address;
    connection->is_busy = false;

    sl_log_init(&connection->log, log->min_level, log->log_fd);
    sl_log_set_pid(&connection->log, getpid());
    sl_log_set_ip_address_port(&connection->log, &address);

    if (connection->arena.first == NULL) {
        sl_arena_init(&connection->arena, arena_preallocate);
    } else {
        sl_arena_rewind(&connection->arena);
    }

    sl_fcgi_parser_init(&connection->parser, &connection->arena, &connection->log);
    sl_fcgi_request_init(&connection->request, &connection->arena, &connection->log, param_hashtable_size);
}

sl_net_connection *sl_net_find_connection(sl_net_connection *connections, size_t max_connections, int socket_fd)
{
    for (size_t n = 0; n < max_connections; n ++) {
        if (connections[n].socket_fd == socket_fd) {
            return &connections[n];
        }
    }

    return NULL;
}

sl_net_connection *sl_net_find_free_connection(sl_net_connection *connections, size_t max_connections)
{
    for (size_t n = 0; n < max_connections; n ++) {
        if (connections[n].is_busy == false) {
            return &connections[n];
        }
    }

    return NULL;
}

void sl_net_destroy_connections(sl_net_connection *connections, size_t max_connections)
{
    for (size_t n = 0; n < max_connections; n ++) {
        if (connections[n].arena.first != NULL) {
            sl_arena_destroy(&connections[n].arena);
        }
    }
}
