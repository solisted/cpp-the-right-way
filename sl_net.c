#include "sl_net.h"

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
