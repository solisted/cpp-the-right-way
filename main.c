#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sl_arena.h"
#include "sl_log.h"
#include "sl_net.h"
#include "sl_fcgi.h"

#define SL_NET_LISTEN_BACKLOG   1024
#define SL_NET_RECV_BUFFER_SIZE 10240
#define SL_NET_IP_ADDRESS_SIZE  16

#define SL_MAIN_ARENA_PREALLOCATE 102400

#define SL_MAIN_MASTER_PROCESS_NAME "cpptrw: master process"
#define SL_MAIN_WORKER_PROCESS_NAME "cpptrw: worker process"

#define SL_MAIN_FCGI_RESPONSE "Content-Type: text/plain\r\n\r\nOK\n"

#define SL_MAIN_MAX_CONNECTIONS 1024
#define SL_MAIN_MAX_EVENTS       256
#define SL_MAIN_MAX_PROCESSES      2

static volatile bool sl_main_running = true;

int sl_main_request_send_response(sl_fcgi_request *request, int connection_socket, void *buffer, uint16_t length)
{
    ssize_t bytes_sent;

    sl_fcgi_msg_header stdout_header = {
        .version = SL_FCGI_VERSION,
        .type = SL_FCGI_TYPE_STDOUT,
        .request_id = htons(request->request_id),
        .content_length = htons(length),
        .padding_length = 0,
        .reserved = 0
    };

    sl_fcgi_msg_header empty_stdout_header = {
        .version = SL_FCGI_VERSION,
        .type = SL_FCGI_TYPE_STDOUT,
        .request_id = htons(request->request_id),
        .content_length = 0,
        .padding_length = 0,
        .reserved = 0
    };

    sl_fcgi_msg_header end_header = {
        .version = SL_FCGI_VERSION,
        .type = SL_FCGI_TYPE_END_REQUEST,
        .request_id = htons(request->request_id),
        .content_length = htons(sizeof(sl_fcgi_msg_end)),
        .padding_length = 0,
        .reserved = 0
    };

    sl_fcgi_msg_end end_message = {0};

    struct iovec buffers[5] = {
        { .iov_base = &stdout_header,       .iov_len = sizeof(sl_fcgi_msg_header) },
        { .iov_base = buffer,               .iov_len = length },
        { .iov_base = &empty_stdout_header, .iov_len = sizeof(sl_fcgi_msg_header)},
        { .iov_base = &end_header,          .iov_len = sizeof(sl_fcgi_msg_header)},
        { .iov_base = &end_message,         .iov_len = sizeof(sl_fcgi_msg_end)}
    };

    bytes_sent = writev(connection_socket, buffers, sizeof(buffers) / sizeof(struct iovec));
    if (bytes_sent == -1) {
        sl_log_write(request->log, SL_LOG_ERROR, "writev()");
        return -1;
    }

    sl_log_write(request->log, SL_LOG_INFO, "Sent $ bytes response", bytes_sent);

    return 0;
}

int sl_main_request_execute(sl_fcgi_request *request, int connection_socket)
{
    if (sl_main_request_send_response(request, connection_socket, SL_MAIN_FCGI_RESPONSE, strlen(SL_MAIN_FCGI_RESPONSE)) == -1) {
        return -1;
    }

    return 0;
}

void sl_main_parse_buffer(sl_fcgi_request *request, sl_fcgi_parser *parser, int connection_socket, uint8_t *buffer, size_t length)
{
    size_t bytes_parsed = 0, previous = 0;

    while (bytes_parsed < length) {
        bytes_parsed += sl_fcgi_parser_parse(parser, buffer + bytes_parsed, length - bytes_parsed);

        sl_log_write(request->log, SL_LOG_INFO, "Parsed $ bytes", bytes_parsed - previous);

        if (parser->state == SL_FCGI_PARSER_STATE_ERROR) {
            sl_log_write(request->log, SL_LOG_ERROR, "Error parsing FCGI message");
            break;
        }

        if (parser->state == SL_FCGI_PARSER_STATE_FINISHED) {
            sl_log_write(parser->log, SL_LOG_INFO, "Received FCGI message");
            sl_fcgi_request_process(request, parser);
            if (request->state == SL_FCGI_REQUEST_STATE_ERROR) {
                sl_log_write(parser->log, SL_LOG_ERROR, "FCGI request error");
                break;
            }

            if (request->state == SL_FCGI_REQUEST_STATE_FINISHED) {
                sl_log_write(parser->log, SL_LOG_INFO, "FCGI request complete");
                sl_main_request_execute(request, connection_socket);
                break;
            }

            sl_fcgi_parser_init(parser, parser->arena, parser->log);
        }

        previous = bytes_parsed;
    }
}

int sl_main_process_connection(sl_net_connection *connection)
{
    uint8_t recv_buffer[SL_NET_RECV_BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = recv(connection->socket_fd, recv_buffer, SL_NET_RECV_BUFFER_SIZE, 0)) > 0) {
        sl_log_write(&connection->log, SL_LOG_INFO, "Received $ bytes", bytes_read);

        sl_main_parse_buffer(&connection->request, &connection->parser, connection->socket_fd, recv_buffer, bytes_read);
        if (connection->request.state == SL_FCGI_REQUEST_STATE_ERROR || connection->parser.state == SL_FCGI_PARSER_STATE_ERROR) {
            break;
        }

        if (bytes_read < SL_NET_RECV_BUFFER_SIZE) {
            break;
        }
    }

    if (connection->request.state == SL_FCGI_REQUEST_STATE_ERROR || connection->parser.state == SL_FCGI_PARSER_STATE_ERROR) {
        return 0;
    }

    if (connection->request.state == SL_FCGI_REQUEST_STATE_FINISHED ||
        (connection->request.flags & SL_FCGI_FLAG_KEEP_CONN) == SL_FCGI_FLAG_KEEP_CONN) {
        sl_log_write(&connection->log, SL_LOG_INFO, "Reusing connection");

        sl_net_init_connection(connection, &connection->log, connection->socket_fd, connection->address, SL_MAIN_ARENA_PREALLOCATE);
        connection->is_busy = true;

        return 1;
    }

    if (bytes_read == -1) {
        sl_log_write(&connection->log, SL_LOG_ERROR, "recv()");
    } else if (bytes_read == 0) {
        sl_log_write(&connection->log, SL_LOG_INFO, "Remote host closed connection");
    }

    return 0;
}

void sl_main_signal_handler(int signal_number)
{
    switch (signal_number) {
        case SIGINT:
            sl_main_running = false;
            break;
        default:
            break;
    }
}

int sl_main_init_signals(void)
{
    if (signal(SIGINT, &sl_main_signal_handler) == SIG_ERR) {
        return -1;
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        return -1;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return -1;
    }

    return 0;
}

void sl_main_set_process_name(int argc, char *argv[], char *env[], char *name)
{
    size_t available = 0, length = strlen(name) + 1;

    if (argc >= 1) {
        available += strlen(argv[0]);
    }

    for (int n = 0; env[n] != NULL; n ++) {
        available += strlen(env[0]);
    }

    if (length > available) {
        return;
    }

    memcpy(argv[0], name, length);
}

void sl_main_event_loop(sl_log *log, int server_socket)
{
    sl_net_connection connections[SL_MAIN_MAX_CONNECTIONS] = {0};
    struct epoll_event event, events[SL_MAIN_MAX_EVENTS];

    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    int epoll_instance = epoll_create1(0);
    if (epoll_instance == -1) {
        sl_log_write(log, SL_LOG_ERROR, "epoll_create1()");
        exit(EXIT_FAILURE);
    }

    event.events = EPOLLIN;
    event.data.fd = server_socket;

    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, server_socket, &event) == -1) {
        sl_log_write(log, SL_LOG_ERROR, "epoll_ctl()");
        exit(EXIT_FAILURE);
    }

    while (sl_main_running == true) {
        int num_events = epoll_wait(epoll_instance, events, SL_MAIN_MAX_EVENTS, -1);
        if (num_events == -1 && errno != EINTR) {
            sl_log_write(log, SL_LOG_ERROR, "epoll_wait()");
            continue;
        }

        for (int n = 0; n < num_events; n ++) {
            if (events[n].data.fd == server_socket) {
                int client_socket = accept(server_socket, (struct sockaddr*) &client_address, &client_address_size);
                if (client_socket == -1 && errno == EAGAIN) {
                    continue;
                } else if (client_socket == -1) {
                    sl_log_write(log, SL_LOG_ERROR, "accept()");
                    continue;
                }

                if (sl_net_set_nonblocking_socket(client_socket) == -1) {
                    sl_log_write(log, SL_LOG_ERROR, "fcntl()");
                    close(client_socket);
                    continue;
                }

                sl_net_connection *connection = sl_net_find_free_connection(connections, SL_MAIN_MAX_CONNECTIONS);
                if (connection == NULL) {
                    sl_log_write(log, SL_LOG_ERROR, "No free connections left in the pool");
                    close(client_socket);
                    continue;
                }

                event.events = EPOLLIN;
                event.data.fd = client_socket;

                if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, client_socket, &event) == -1) {
                    sl_log_write(log, SL_LOG_ERROR, "epoll_ctl()");
                    close(client_socket);
                    continue;
                }

                sl_net_init_connection(connection, log, client_socket, client_address, SL_MAIN_ARENA_PREALLOCATE);
                sl_log_write(&connection->log, SL_LOG_INFO, "Connection established");
                connection->is_busy = true;
                continue;
            }

            sl_net_connection *connection = sl_net_find_connection(connections, SL_MAIN_MAX_CONNECTIONS, events[n].data.fd);
            if (connection == NULL) {
                sl_log_write(log, SL_LOG_ERROR, "Unable to find connection in the pool for socket $", events[n].data.fd);

                if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL, events[n].data.fd, NULL) == -1) {
                    sl_log_write(log, SL_LOG_ERROR, "epoll_ctl()");
                }

                close(events[n].data.fd);
                continue;
            }

            if (sl_main_process_connection(connection) == 0) {
                connection->is_busy = false;

                if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL, events[n].data.fd, NULL) == -1) {
                    sl_log_write(log, SL_LOG_ERROR, "epoll_ctl()");
                }

                sl_log_write(&connection->log, SL_LOG_INFO, "Connection closed");

                close(events[n].data.fd);
            }
       }
    }

    sl_log_write(log, SL_LOG_INFO, "Terminating worker process");
    sl_net_destroy_connections(connections, SL_MAIN_MAX_CONNECTIONS);
}
int main(int argc, char *argv[], char *env[])
{
    sl_log log;

    sl_main_set_process_name(argc, argv, env, SL_MAIN_MASTER_PROCESS_NAME);

    sl_log_init(&log, SL_LOG_ERROR, STDOUT_FILENO);
    sl_log_set_pid(&log, getpid());

    if (sl_main_init_signals() == -1) {
        sl_log_write(&log, SL_LOG_ERROR, "signal()");
        exit(EXIT_FAILURE);
    }

    int server_socket = sl_net_create_listen_socket(INADDR_ANY, 9000, SL_NET_LISTEN_BACKLOG);
    if (server_socket == -1) {
        sl_log_write(&log, SL_LOG_ERROR, "sl_main_create_socket()");
        exit(EXIT_FAILURE);
    }

    if (sl_net_set_nonblocking_socket(server_socket) == -1) {
        sl_log_write(&log, SL_LOG_ERROR, "fcntl()");
        exit(EXIT_FAILURE);
    }

    for (int n = 0; n < SL_MAIN_MAX_PROCESSES; n ++) {
        pid_t pid = fork();
        if (pid == -1) {
            sl_log_write(&log, SL_LOG_ERROR, "fork()");
            exit(EXIT_FAILURE);
        }

        if (pid > 0) {
            sl_log_write(&log, SL_LOG_INFO, "Spawned worker process $", pid);
            continue;
        }

        sl_main_set_process_name(argc, argv, env, SL_MAIN_WORKER_PROCESS_NAME);
        sl_log_set_pid(&log, getpid());

        sl_main_event_loop(&log, server_socket);

        return EXIT_SUCCESS;
    }

    close(server_socket);
    wait(NULL);

    sl_log_write(&log, SL_LOG_INFO, "Terminating master process");

    return EXIT_SUCCESS;
}
