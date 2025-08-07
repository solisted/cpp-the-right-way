#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <signal.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sl_arena.h"
#include "sl_net.h"
#include "sl_fcgi.h"

#define SL_NET_LISTEN_BACKLOG   1024
#define SL_NET_RECV_BUFFER_SIZE 10240
#define SL_NET_IP_ADDRESS_SIZE  16

#define SL_MAIN_ARENA_PREALLOCATE 1024*1024
#define SL_MAIN_PROCESS_COUNT 8

#define SL_MAIN_FCGI_RESPONSE "Content-Type: text/plain\r\n\r\nOK\r\n"

int sl_main_request_send_response(sl_fcgi_request *request, int connection_socket, void *buffer, uint16_t length)
{
    ssize_t bytes_sent;
    size_t output_length = sizeof(sl_fcgi_msg_header) * 3 + sizeof(sl_fcgi_msg_end) + length;

    uint8_t *output_buffer = sl_arena_allocate(request->arena, output_length);
    if (output_buffer == NULL) {
        warn("sl_arena_allocate()");
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
        warn("send()");
        return -1;
    }

    warnx("sl_main_request_send_response(): Send %lu bytes response", bytes_sent);

    return 0;
}

int sl_main_request_execute(sl_fcgi_request *request, int connection_socket)
{
    if (sl_main_request_send_response(request, connection_socket, SL_MAIN_FCGI_RESPONSE, strlen(SL_MAIN_FCGI_RESPONSE)) == -1) {
        warn("sl_main_request_send_response()");
        return -1;
    }

    return 0;
}

void sl_main_parse_buffer(sl_fcgi_request *request, sl_fcgi_parser *parser, int connection_socket, uint8_t *buffer, size_t length, uint8_t *address, uint16_t port)
{
    ssize_t bytes_parsed = 0, previous = 0;

    while (bytes_parsed < length) {
        bytes_parsed += sl_fcgi_parser_parse(parser, buffer + bytes_parsed, length - bytes_parsed);

        warnx("recv(): Parsed %ld bytes for %s:%u", bytes_parsed - previous, address, port);

        if (parser->state == SL_FCGI_PARSER_STATE_ERROR) {
            warnx("sl_fcgi_parser_parse(): FCGI parse error for %s:%u - request id: %u, type: %u, size: 8/%u/%u",
                  address, port, parser->message_header.request_id, parser->message_header.type,
                  parser->message_header.content_length, parser->message_header.padding_length);
            break;
        }

        if (parser->state == SL_FCGI_PARSER_STATE_FINISHED) {
            warnx("sl_fcgi_parser_parse(): FCGI message for %s:%u - request id: %u, type: %u, size: 8/%u/%u",
                  address, port, parser->message_header.request_id, parser->message_header.type,
                  parser->message_header.content_length, parser->message_header.padding_length);
            sl_fcgi_request_process(request, parser);
            if (request->state == SL_FCGI_REQUEST_STATE_ERROR) {
                warnx("sl_fcgi_request_process(): FCGI request error for %s:%u", address, port);
                break;
            }

            if (request->state == SL_FCGI_REQUEST_STATE_FINISHED) {
                warnx("sl_fcgi_request_process(): FCGI request complete for %s:%u", address, port);
                sl_main_request_execute(request, connection_socket);
                break;
            }

            sl_fcgi_parser_init(parser, parser->arena);
        }

        previous = bytes_parsed;
    }
}

int sl_main_process_connection(sl_arena *arena, int connection_socket, uint8_t *address, uint16_t port)
{
    sl_fcgi_parser parser;
    sl_fcgi_request request;

    uint8_t recv_buffer[SL_NET_RECV_BUFFER_SIZE];
    ssize_t bytes_read;

    sl_fcgi_request_init(&request, arena);
    sl_fcgi_parser_init(&parser, arena);

    while ((bytes_read = recv(connection_socket, recv_buffer, SL_NET_RECV_BUFFER_SIZE, 0)) > 0) {
        warnx("recv(): Received %ld bytes from %s:%u", bytes_read, address, port);

        sl_main_parse_buffer(&request, &parser, connection_socket, recv_buffer, bytes_read, address, port);
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
        warn("recv(): Error while reading from %s:%u", address, port);
    } else if (bytes_read == 0) {
        warnx("recv(): Connection closed while reading from %s:%u", address, port);
    }

    return 0;
}

void sl_main_signal_handler(int signal_number)
{
    switch (signal_number) {
        case SIGINT:
            warnx("sl_main_signal_handler(): Received SIGINT, terminating program");
            exit(EXIT_FAILURE);
            break;
        default:
            break;
    }
}

int main(int argc, char *argv[])
{
    sl_arena arena;

    int server_socket, client_socket;
    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    uint8_t client_ip_address[SL_NET_IP_ADDRESS_SIZE];
    uint16_t client_port;

    if (signal(SIGINT, &sl_main_signal_handler) == SIG_ERR) {
        err(EXIT_FAILURE, "signal()");
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        err(EXIT_FAILURE, "signal()");
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        err(EXIT_FAILURE, "signal()");
    }

    server_socket = sl_net_create_listen_socket(INADDR_ANY, 9000, SL_NET_LISTEN_BACKLOG);
    if (server_socket == -1) {
        err(EXIT_FAILURE, "sl_main_create_socket()");
    }

    for (int n = 0; n < SL_MAIN_PROCESS_COUNT; n ++) {
        pid_t pid = fork();
        if (pid == -1) {
            err(EXIT_FAILURE, "fork()");
        }

        if (pid > 0) {
            warnx("main(): Spawned child process: %d", pid);
            continue;
        }

        sl_arena_init(&arena, SL_MAIN_ARENA_PREALLOCATE);

        while (1) {
            client_socket = accept(server_socket, (struct sockaddr*) &client_address, &client_address_size);

            memcpy(client_ip_address, inet_ntoa(client_address.sin_addr), SL_NET_IP_ADDRESS_SIZE);
            client_port = ntohs(client_address.sin_port);

            warnx("accept(): Got connection from %s:%u", client_ip_address, client_port);

            while (sl_main_process_connection(&arena, client_socket, client_ip_address, client_port) == 1) {
                warnx("accept(): Reusing connection for %s:%u", client_ip_address, client_port);
                sl_arena_rewind(&arena);
            }

            sl_arena_rewind(&arena);

            warnx("close(): Closing connection to %s:%u", client_ip_address, client_port);
            close(client_socket);
        }

        sl_arena_destroy(&arena);
        exit(EXIT_SUCCESS);
    }

    close(server_socket);
    wait(NULL);

    return EXIT_SUCCESS;
}
