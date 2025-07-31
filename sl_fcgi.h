#ifndef SL_FGI_H
#define SL_FGI_H

#include <string.h>

#include "sl_arena.h"

#define SL_FCGI_TYPE_BEGIN_REQUEST 1
#define SL_FCGI_TYPE_PARAMS        4
#define SL_FCGI_TYPE_STDIN         5

typedef enum sl_fcgi_parser_state sl_fcgi_parser_state;
typedef enum sl_fcgi_request_state sl_fcgi_request_state;

typedef struct sl_fcgi_msg_header sl_fcgi_msg_header;
typedef struct sl_fcgi_parser sl_fcgi_parser;
typedef struct sl_fcgi_msg_begin sl_fcgi_msg_begin;
typedef struct sl_fcgi_msg_param sl_fcgi_msg_param;
typedef struct sl_fcgi_msg_stdin sl_fcgi_msg_stdin;
typedef struct sl_fcgi_request sl_fcgi_request;

enum sl_fcgi_parser_state {
    SL_FGI_PARSER_STATE_VERSION,
    SL_FGI_PARSER_STATE_TYPE,
    SL_FGI_PARSER_STATE_REQUEST_ID_B1,
    SL_FGI_PARSER_STATE_REQUEST_ID_B0,
    SL_FGI_PARSER_STATE_CONTENT_LENGTH_B1,
    SL_FGI_PARSER_STATE_CONTENT_LENGTH_B0,
    SL_FGI_PARSER_STATE_PADDING_LENGTH,
    SL_FGI_PARSER_STATE_PADDING_RESERVED,

    SL_FGI_PARSER_STATE_BEGIN_ROLE_B1,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_B0,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_FLAGS,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP5,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP4,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP3,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP2,
    SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP1,

    SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B3,
    SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B2,
    SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B1,
    SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B0,
    SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B3,
    SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B2,
    SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B1,
    SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B0,
    SL_FCGI_PARSER_STATE_PARAM_NAME_DATA,
    SL_FCGI_PARSER_STATE_PARAM_VALUE_DATA,
    SL_FCGI_PARSER_STATE_PARAM_PADDING,

    SL_FCGI_PARSER_STATE_STDIN_DATA,
    SL_FCGI_PARSER_STATE_STDIN_PADDING,

    SL_FCGI_PARSER_STATE_FINISHED,
    SL_FCGI_PARSER_STATE_ERROR
};

enum sl_fcgi_request_state {
    SL_FCGI_REQUEST_STATE_BEGIN,
    SL_FCGI_REQUEST_STATE_PARAM_OR_STDIN,
    SL_FCGI_REQUEST_STATE_PARAM,
    SL_FCGI_REQUEST_STATE_STDIN,
    SL_FCGI_REQUEST_STATE_PROCESS,
    SL_FCGI_REQUEST_STATE_RESPOND,

    SL_FCGI_REQUEST_STATE_FINISHED,
    SL_FCGI_REQUEST_STATE_ERROR
};

struct sl_fcgi_msg_header {
    uint8_t version;
    uint8_t type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t padding_length;
    uint8_t reserved;
};

struct sl_fcgi_msg_begin {
    uint16_t role;
    uint8_t flags;
    uint8_t reserved[5];
};

struct sl_fcgi_msg_param {
    sl_fcgi_msg_param *next;
    uint32_t name_length;
    uint32_t value_length;
    uint8_t *name;
    uint8_t *value;
};

struct sl_fcgi_msg_stdin {
    uint16_t length;
    uint8_t *data;
};

struct sl_fcgi_parser {
    sl_fcgi_parser_state state;
    size_t read_counter;
    size_t message_size;
    sl_arena *arena;
    sl_fcgi_msg_header message_header;
    sl_fcgi_msg_begin begin_message;
    sl_fcgi_msg_param *first_param;
    sl_fcgi_msg_param *last_param;
    sl_fcgi_msg_stdin stdin_stream;
};

struct sl_fcgi_request {
    sl_fcgi_request_state state;
    sl_arena *arena;
    sl_fcgi_msg_param *first_param;
    sl_fcgi_msg_param *last_param;
    sl_fcgi_msg_stdin stdin_stream;
};

void sl_fcgi_parser_init(sl_fcgi_parser *parser, sl_arena *arena);
ssize_t sl_fcgi_parser_parse(sl_fcgi_parser *parser, uint8_t *buffer, size_t length);

void sl_fcgi_request_init(sl_fcgi_request *request, sl_arena *arena);
void sl_fcgi_request_process(sl_fcgi_request *request, sl_fcgi_parser *parser);

#endif
