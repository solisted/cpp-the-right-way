#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sl_arena.h"
#include "sl_net.h"
#include "sl_fcgi.h"

#define SL_NET_LISTEN_BACKLOG   128
#define SL_NET_RECV_BUFFER_SIZE 1024
#define SL_NET_IP_ADDRESS_SIZE  16

#define SL_MAIN_ARENA_PREALLOCATE 1024*1024

static int sl_main_running = 1;

void sl_main_dump_buffer(uint8_t *buffer, size_t length)
{
    for (size_t n = 0; n < length; n += 16) {
        printf("%08zX ", n);

        for (size_t c = n; c < n + 16 && c < length; c ++) {
            printf("%02X ", buffer[c]);
        }

        for (size_t c = n; c < n + 16 && c < length; c ++) {
            printf("%c", (buffer[c] >= 0x20 && buffer[c] < 0x7f) ? buffer[c] : '.');
        }

        printf("\n");
    }
}

void sl_main_dump_parser(sl_fcgi_parser *parser)
{
    printf("------------\nFCGI Header:\n");
    printf("- Version: %u\n", parser->message_header.version);
    printf("- Type: %u\n", parser->message_header.type);
    printf("- Request ID: %u\n", parser->message_header.request_id);
    printf("- Content Length: %u\n", parser->message_header.content_length);
    printf("- Padding Length: %u\n", parser->message_header.padding_length);

    switch (parser->message_header.type) {
        case SL_FCGI_TYPE_BEGIN_REQUEST:
            printf("FCGI Begin Request:\n");
            printf("- Role: %u\n", parser->begin_message.role);
            printf("- Flags: %u\n", parser->begin_message.flags);
            break;
        case SL_FCGI_TYPE_PARAMS:
            if (parser->first_param == NULL) {
                printf("FCGI Param:\n(null)\n");
                break;
            }

            for (sl_fcgi_msg_param *param = parser->first_param; param != NULL; param = param->next) {
                printf("FCGI Param: %s=%s\n", param->name, param->value);
            }
            break;
        case SL_FCGI_TYPE_STDIN:
            printf("FCGI Stdin:\n%s\n", parser->stdin_stream.data);
            break;
    }
}

void sl_main_parse_buffer(sl_fcgi_parser *parser, uint8_t *buffer, size_t length, uint8_t *address, uint16_t port)
{
    ssize_t bytes_parsed = 0, previous = 0;

    while (bytes_parsed < length) {
        bytes_parsed += sl_fcgi_parser_parse(parser, buffer + bytes_parsed, length - bytes_parsed);

        warnx("recv(): Parsed %ld bytes for %s:%u", bytes_parsed - previous, address, port);

        if (parser->state == SL_FCGI_PARSER_STATE_ERROR) {
            warnx("sl_fcgi_parser_parse(): FCGI parse error for %s:%u - request id: %u, type: %u, size: 8/%u/%u",
                  address, port, parser->message_header.request_id, parser->message_header.type,
                  parser->message_header.content_length, parser->message_header.padding_length);
            sl_main_dump_parser(parser);
            break;
        }

        if (parser->state == SL_FCGI_PARSER_STATE_FINISHED) {
            warnx("sl_fcgi_parser_parse(): FCGI message for %s:%u - request id: %u, type: %u, size: 8/%u/%u",
                  address, port, parser->message_header.request_id, parser->message_header.type,
                  parser->message_header.content_length, parser->message_header.padding_length);
            sl_main_dump_parser(parser);
            sl_arena_rewind(parser->arena);
            sl_fcgi_parser_init(parser, parser->arena);
        }

        previous = bytes_parsed;
    }
}

void sl_main_process_connection(sl_arena *arena, int connection_socket, uint8_t *address, uint16_t port)
{
    sl_fcgi_parser parser;
    uint8_t recv_buffer[SL_NET_RECV_BUFFER_SIZE];
    ssize_t bytes_read;

    sl_fcgi_parser_init(&parser, arena);

    while ((bytes_read = recv(connection_socket, recv_buffer, SL_NET_RECV_BUFFER_SIZE, 0)) > 0) {
        warnx("recv(): Received %ld bytes from %s:%u", bytes_read, address, port);

        sl_main_parse_buffer(&parser, recv_buffer, bytes_read, address, port);
        if (parser.state == SL_FCGI_PARSER_STATE_ERROR) {
            break;
        }

        if (bytes_read < SL_NET_RECV_BUFFER_SIZE) {
            break;
        }
    }

    if (bytes_read == -1) {
        warn("recv(): Error while reading from  %s:%u", address, port);
    }
}

void sl_main_signal_handler(int signal_number)
{
    switch (signal_number) {
        case SIGINT:
            sl_main_running = 0;
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

    signal(SIGINT, &sl_main_signal_handler);

    server_socket = sl_net_create_listen_socket(INADDR_ANY, 9000, SL_NET_LISTEN_BACKLOG);
    if (server_socket == -1) {
        err(EXIT_FAILURE, "sl_main_create_socket()");
    }

    sl_arena_init(&arena, SL_MAIN_ARENA_PREALLOCATE);

    while (sl_main_running) {
        client_socket = accept(server_socket, (struct sockaddr*) &client_address, &client_address_size);
        if (client_socket == -1) {
            warn("accept()");
            continue;
        }
        memcpy(client_ip_address, inet_ntoa(client_address.sin_addr), SL_NET_IP_ADDRESS_SIZE);
        client_port = ntohs(client_address.sin_port);

        warnx("accept(): Got connection from %s:%u", client_ip_address, client_port);

        sl_main_process_connection(&arena, client_socket, client_ip_address, client_port);
        sl_arena_rewind(&arena);

        warnx("close(): Closing connection to %s:%u", client_ip_address, client_port);
        close(client_socket);
    }

    sl_arena_destroy(&arena);
    close(server_socket);

    warnx("main(): Program interrupted by SIGINT");

    return EXIT_SUCCESS;
}
