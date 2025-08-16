#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
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

#define SL_MAIN_ARENA_PREALLOCATE 1024*1024
#define SL_MAIN_PROCESS_COUNT 8

#define SL_MAIN_MASTER_PROCESS_NAME "cpptrw: master process"
#define SL_MAIN_WORKER_PROCESS_NAME "cpptrw: worker process"

#define SL_MAIN_FCGI_RESPONSE "Content-Type: text/plain\r\n\r\nOK\n"

int sl_main_request_send_response(sl_fcgi_request *request, int connection_socket, void *buffer, uint16_t length)
{
    ssize_t bytes_sent;
    size_t output_length = sizeof(sl_fcgi_msg_header) * 3 + sizeof(sl_fcgi_msg_end) + length;

    uint8_t *output_buffer = sl_arena_allocate(request->arena, output_length);
    if (output_buffer == NULL) {
        sl_log_write(request->log, SL_LOG_ERROR, "sl_arena_allocate()");
        return -1;
    }

    sl_fcgi_msg_header stdout_header = {
        .version = SL_FCGI_VERSION,
        .type = SL_FCGI_TYPE_STDOUT,
        .request_id = htons(request->request_id),
        .content_length = htons(length),
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

    memcpy(output_buffer, &stdout_header, sizeof(sl_fcgi_msg_header));
    memcpy(output_buffer + sizeof(sl_fcgi_msg_header), buffer, length);

    stdout_header.content_length = 0;
    memcpy(output_buffer + sizeof(sl_fcgi_msg_header) + length, &stdout_header, sizeof(sl_fcgi_msg_header));

    memcpy(output_buffer + sizeof(sl_fcgi_msg_header) * 2 + length, &end_header, sizeof(sl_fcgi_msg_header));
    memcpy(output_buffer + sizeof(sl_fcgi_msg_header) * 3 + length, &end_message, sizeof(sl_fcgi_msg_end));

    bytes_sent = send(connection_socket, output_buffer, output_length, 0);
    if (bytes_sent == -1) {
        sl_log_write(request->log, SL_LOG_ERROR, "send()");
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
    ssize_t bytes_parsed = 0, previous = 0;

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

int sl_main_process_connection(sl_arena *arena, sl_log *log, int connection_socket)
{
    sl_fcgi_parser parser;
    sl_fcgi_request request;

    uint8_t recv_buffer[SL_NET_RECV_BUFFER_SIZE];
    ssize_t bytes_read;

    sl_fcgi_request_init(&request, arena, log);
    sl_fcgi_parser_init(&parser, arena, log);

    while ((bytes_read = recv(connection_socket, recv_buffer, SL_NET_RECV_BUFFER_SIZE, 0)) > 0) {
        sl_log_write(log, SL_LOG_INFO, "Received $ bytes", bytes_read);

        sl_main_parse_buffer(&request, &parser, connection_socket, recv_buffer, bytes_read);
        if (request.state == SL_FCGI_REQUEST_STATE_ERROR || parser.state == SL_FCGI_PARSER_STATE_ERROR) {
            break;
        }

        if (bytes_read < SL_NET_RECV_BUFFER_SIZE) {
            break;
        }
    }

    if ((request.flags & SL_FCGI_FLAG_KEEP_CONN) == SL_FCGI_FLAG_KEEP_CONN) {
        return 1;
    }

    if (bytes_read == -1) {
        sl_log_write(log, SL_LOG_ERROR, "recv()");
    } else if (bytes_read == 0) {
        sl_log_write(log, SL_LOG_INFO, "Remote host closed connection");
    }

    return 0;
}

void sl_main_signal_handler(int signal_number)
{
    switch (signal_number) {
        case SIGINT:
            exit(EXIT_SUCCESS);
        default:
            break;
    }
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

int main(int argc, char *argv[], char *env[])
{
    sl_arena arena;
    sl_log log;

    int server_socket, client_socket;
    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    sl_main_set_process_name(argc, argv, env, SL_MAIN_MASTER_PROCESS_NAME);

    sl_log_init(&log, SL_LOG_ERROR, STDOUT_FILENO);
    sl_log_set_pid(&log, getpid());

    if (signal(SIGINT, &sl_main_signal_handler) == SIG_ERR) {
        sl_log_write(&log, SL_LOG_ERROR, "signal()");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        sl_log_write(&log, SL_LOG_ERROR, "signal()");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        sl_log_write(&log, SL_LOG_ERROR, "signal()");
        exit(EXIT_FAILURE);
    }

    server_socket = sl_net_create_listen_socket(INADDR_ANY, 9000, SL_NET_LISTEN_BACKLOG);
    if (server_socket == -1) {
        sl_log_write(&log, SL_LOG_ERROR, "sl_main_create_socket()");
        exit(EXIT_FAILURE);
    }

    for (int n = 0; n < SL_MAIN_PROCESS_COUNT; n ++) {
        pid_t pid = fork();
        if (pid == -1) {
            sl_log_write(&log, SL_LOG_ERROR, "fork()");
            exit(EXIT_FAILURE);
        }

        if (pid > 0) {
            continue;
        }

        sl_main_set_process_name(argc, argv, env, SL_MAIN_WORKER_PROCESS_NAME);

        sl_log_set_pid(&log, getpid());
        sl_arena_init(&arena, SL_MAIN_ARENA_PREALLOCATE);

        while (1) {
            client_socket = accept(server_socket, (struct sockaddr*) &client_address, &client_address_size);

            sl_log_set_ip_address_port(&log, &client_address);

            sl_log_write(&log, SL_LOG_INFO, "Connection established");

            while (sl_main_process_connection(&arena, &log, client_socket) == 1) {
                sl_log_write(&log, SL_LOG_INFO, "Connection reused");
                sl_arena_rewind(&arena);
            }

            sl_arena_rewind(&arena);

            sl_log_write(&log, SL_LOG_INFO, "Connection closed");
            close(client_socket);
        }

        sl_arena_destroy(&arena);
        exit(EXIT_SUCCESS);
    }

    close(server_socket);
    wait(NULL);

    return EXIT_SUCCESS;
}
