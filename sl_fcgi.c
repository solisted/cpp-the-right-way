#include "sl_fcgi.h"
#include "sl_string.h"

#include <stdio.h>

#define SL_FCGI_RESPONSE_HEADER_PREALLOCATE 64

void sl_fcgi_parser_init(sl_fcgi_parser *parser, sl_arena *arena, sl_log *log)
{
    *parser = (sl_fcgi_parser) {0};

    parser->state = SL_FGI_PARSER_STATE_VERSION;
    parser->arena = arena;
    parser->log = log;
}

sl_fcgi_parser_state sl_fcgi_parser_dispatch_type(uint8_t type)
{
    switch (type) {
        case SL_FCGI_TYPE_BEGIN_REQUEST:
            return SL_FGI_PARSER_STATE_BEGIN_ROLE_B1;
        case SL_FCGI_TYPE_PARAMS:
            return SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B3;
        case SL_FCGI_TYPE_STDIN:
            return SL_FCGI_PARSER_STATE_STDIN_DATA;
        default:
            return SL_FCGI_PARSER_STATE_ERROR;
    }

    return SL_FCGI_PARSER_STATE_ERROR;
}

void sl_fcgi_parser_parse_begin_request(sl_fcgi_parser *parser, uint8_t octet)
{
    switch (parser->state) {
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_B1:
            parser->begin_message.role = ((uint8_t) octet) << 8;
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_B0;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_B0:
            parser->begin_message.role |= octet;
            parser->state = SL_FGI_PARSER_STATE_BEGIN_FLAGS;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_FLAGS:
            parser->begin_message.flags = octet;
            parser->state = SL_FGI_PARSER_STATE_BEGIN_SKIP5;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_SKIP5:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_SKIP4;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_SKIP4:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_SKIP3;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_SKIP3:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_SKIP2;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_SKIP2:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_SKIP1;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_SKIP1:
            parser->message_size = 16;
            parser->state = SL_FCGI_PARSER_STATE_FINISHED;
            break;
        default:
            parser->state = SL_FCGI_PARSER_STATE_ERROR;
            break;
    }
}

void sl_fcgi_parser_parse_params(sl_fcgi_parser *parser, uint8_t octet)
{
    sl_fcgi_msg_param *param;

    while (1) {
        switch (parser->state) {
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B3:
                param = sl_arena_allocate(parser->arena, sizeof(sl_fcgi_msg_param));
                if (param == NULL) {
                    parser->state = SL_FCGI_PARSER_STATE_ERROR;
                    return;
                }

                param->next = NULL;
                param->name_length = 0;
                param->value_length = 0;
                param->name = NULL;
                param->value = NULL;

                if (parser->last_param != NULL) {
                    parser->last_param->next = param;
                }

                parser->last_param = param;

                if (parser->first_param == NULL) {
                    parser->message_size = 0;
                    parser->first_param = parser->last_param;
                }

                if ((octet & 0x80) == 0x80) {
                    parser->last_param->name_length = (((uint8_t) octet) & 0x7f) << 24;
                    parser->message_size += 4;
                    parser->state = SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B2;
                } else {
                    parser->last_param->name_length = octet;
                    parser->message_size ++;
                    parser->state = SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B3;
                }
                return;
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B2:
                parser->last_param->name_length |= ((uint8_t) octet) << 16;
                parser->state = SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B1;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B1:
                parser->last_param->name_length |= ((uint8_t) octet) << 8;
                parser->state = SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B0;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B0:
                parser->last_param->name_length |= octet;
                parser->state = SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B3;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B3:
                if ((octet & 0x80) == 0x80) {
                    parser->last_param->value_length = (((uint8_t) octet) & 0x7f) << 24;
                    parser->message_size += 4;
                    parser->state = SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B2;
                } else {
                    parser->last_param->value_length = octet;
                    parser->message_size ++;
                    parser->state = SL_FCGI_PARSER_STATE_PARAM_NAME_DATA;
                }
                return;
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B2:
                parser->last_param->value_length |= ((uint8_t) octet) << 16;
                parser->state = SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B1;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B1:
                parser->last_param->value_length |= ((uint8_t) octet) << 8;
                parser->state = SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B0;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B0:
                parser->last_param->value_length |= octet;
                parser->state = SL_FCGI_PARSER_STATE_PARAM_NAME_DATA;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_NAME_DATA:
                if (parser->last_param->name == NULL) {
                    parser->last_param->name = sl_arena_allocate(parser->arena, parser->last_param->name_length + 1);
                    if (parser->last_param->name == NULL) {
                        parser->state = SL_FCGI_PARSER_STATE_ERROR;
                        return;
                    }
                    parser->read_counter = parser->last_param->name_length;
                    parser->message_size += parser->last_param->name_length;
                }

                if (parser->read_counter == 0) {
                    parser->last_param->name[parser->last_param->name_length] = 0;
                    parser->state = SL_FCGI_PARSER_STATE_PARAM_VALUE_DATA;
                    continue;
                }

                parser->last_param->name[parser->last_param->name_length - parser->read_counter] = octet;
                parser->read_counter --;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_DATA:
                if (parser->last_param->value == NULL) {
                    parser->last_param->value = sl_arena_allocate(parser->arena, parser->last_param->value_length + 1);
                    if (parser->last_param->value == NULL) {
                        parser->state = SL_FCGI_PARSER_STATE_ERROR;
                        return;
                    }
                    parser->read_counter = parser->last_param->value_length;
                    parser->message_size += parser->last_param->value_length;
                }

                if (parser->read_counter == 0) {
                    parser->last_param->value[parser->last_param->value_length] = 0;

                    sl_string parameter_name = sl_string_init_with_buffer((char *) parser->last_param->name, parser->last_param->name_length);
                    sl_string parameter_value = sl_string_init_with_buffer((char *) parser->last_param->value, parser->last_param->value_length);
                    sl_log_write_format(parser->arena, parser->log, SL_LOG_DEBUG, "FCGI parameter: %S=%S", &parameter_name, &parameter_value);

                    if (parser->message_size == parser->message_header.content_length) {
                        if (parser->message_header.padding_length == 0) {
                            parser->state = SL_FCGI_PARSER_STATE_FINISHED;
                            return;
                        } else {
                            parser->read_counter = parser->message_header.padding_length;
                            parser->state = SL_FCGI_PARSER_STATE_PARAM_PADDING;
                            continue;
                        }
                    }

                    parser->state = SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B3;
                    continue;
                }

                parser->last_param->value[parser->last_param->value_length - parser->read_counter] = octet;
                parser->read_counter --;
                return;
            case SL_FCGI_PARSER_STATE_PARAM_PADDING:
                if (parser->read_counter-- == 0) {
                    parser->state = SL_FCGI_PARSER_STATE_FINISHED;
                }
                return;
            default:
                parser->state = SL_FCGI_PARSER_STATE_ERROR;
                return;
        }
    }
}

void sl_fcgi_parser_parse_stdin(sl_fcgi_parser *parser, uint8_t octet)
{
    while (1) {
        switch (parser->state) {
            case SL_FCGI_PARSER_STATE_STDIN_DATA:
                if (parser->stdin_stream.data == NULL) {
                    parser->stdin_stream.length = parser->message_header.content_length;
                    parser->stdin_stream.data = sl_arena_allocate(parser->arena, parser->stdin_stream.length + 1);
                    if (parser->stdin_stream.data == NULL) {
                        parser->state = SL_FCGI_PARSER_STATE_ERROR;
                        return;
                    }

                    parser->read_counter = parser->stdin_stream.length;
                    parser->message_size += parser->stdin_stream.length;
                }

                if (parser->read_counter == 0) {
                    parser->stdin_stream.data[parser->stdin_stream.length] = 0;

                    sl_string stdin = sl_string_init_with_buffer((char *) parser->stdin_stream.data, parser->stdin_stream.length);
                    sl_log_write_format(parser->arena, parser->log, SL_LOG_DEBUG, "FCGI stdin: %S", &stdin);

                    if (parser->message_header.padding_length > 0) {
                        parser->read_counter = parser->message_header.padding_length;
                        parser->state = SL_FCGI_PARSER_STATE_STDIN_PADDING;
                        continue;
                    }

                    parser->state = SL_FCGI_PARSER_STATE_FINISHED;
                    continue;
                }

                parser->stdin_stream.data[parser->stdin_stream.length - parser->read_counter] = octet;
                parser->read_counter --;
                return;
            case SL_FCGI_PARSER_STATE_STDIN_PADDING:
                if (parser->read_counter-- == 0) {
                    parser->state = SL_FCGI_PARSER_STATE_FINISHED;
                }
                return;
            default:
                parser->state = SL_FCGI_PARSER_STATE_ERROR;
                return;
        }
    }
}

ssize_t sl_fcgi_parser_parse(sl_fcgi_parser *parser, uint8_t *buffer, size_t length)
{
    ssize_t total_parsed = 0;

    while (length-- > 0) {
        uint8_t octet = *(buffer++);

        switch (parser->state) {
            case SL_FGI_PARSER_STATE_VERSION:
                parser->message_header.version = octet;
                parser->state = SL_FGI_PARSER_STATE_TYPE;
                break;
            case SL_FGI_PARSER_STATE_TYPE:
                parser->message_header.type = octet;
                parser->state = SL_FGI_PARSER_STATE_REQUEST_ID_B1;
                break;
            case SL_FGI_PARSER_STATE_REQUEST_ID_B1:
                parser->message_header.request_id = ((uint8_t) octet) << 8;
                parser->state = SL_FGI_PARSER_STATE_REQUEST_ID_B0;
                break;
            case SL_FGI_PARSER_STATE_REQUEST_ID_B0:
                parser->message_header.request_id |= octet;
                parser->state = SL_FGI_PARSER_STATE_CONTENT_LENGTH_B1;
                break;
            case SL_FGI_PARSER_STATE_CONTENT_LENGTH_B1:
                parser->message_header.content_length = ((uint8_t) octet) << 8;
                parser->state = SL_FGI_PARSER_STATE_CONTENT_LENGTH_B0;
                break;
            case SL_FGI_PARSER_STATE_CONTENT_LENGTH_B0:
                parser->message_header.content_length |= octet;
                parser->state = SL_FGI_PARSER_STATE_PADDING_LENGTH;
                break;
            case SL_FGI_PARSER_STATE_PADDING_LENGTH:
                parser->message_header.padding_length = octet;
                parser->state = SL_FGI_PARSER_STATE_PADDING_RESERVED;
                break;
            case SL_FGI_PARSER_STATE_PADDING_RESERVED:
                parser->message_header.reserved = octet;
                if (parser->message_header.content_length == 0) {
                    parser->state = SL_FCGI_PARSER_STATE_FINISHED;
                    return total_parsed + 1;
                }
                parser->state = sl_fcgi_parser_dispatch_type(parser->message_header.type);
                break;
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_B1:
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_B0:
            case SL_FGI_PARSER_STATE_BEGIN_FLAGS:
            case SL_FGI_PARSER_STATE_BEGIN_SKIP5:
            case SL_FGI_PARSER_STATE_BEGIN_SKIP4:
            case SL_FGI_PARSER_STATE_BEGIN_SKIP3:
            case SL_FGI_PARSER_STATE_BEGIN_SKIP2:
            case SL_FGI_PARSER_STATE_BEGIN_SKIP1:
                sl_fcgi_parser_parse_begin_request(parser, octet);
                break;
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B3:
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B2:
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B1:
            case SL_FCGI_PARSER_STATE_PARAM_NAME_LENGTH_B0:
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B3:
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B2:
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B1:
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_LENGTH_B0:
            case SL_FCGI_PARSER_STATE_PARAM_NAME_DATA:
            case SL_FCGI_PARSER_STATE_PARAM_VALUE_DATA:
            case SL_FCGI_PARSER_STATE_PARAM_PADDING:
                sl_fcgi_parser_parse_params(parser, octet);
                if (parser->state == SL_FCGI_PARSER_STATE_FINISHED || parser->state == SL_FCGI_PARSER_STATE_ERROR) {
                    return total_parsed;
                }
                break;
            case SL_FCGI_PARSER_STATE_STDIN_DATA:
            case SL_FCGI_PARSER_STATE_STDIN_PADDING:
                sl_fcgi_parser_parse_stdin(parser, octet);
                if (parser->state == SL_FCGI_PARSER_STATE_FINISHED || parser->state == SL_FCGI_PARSER_STATE_ERROR) {
                    return total_parsed;
                }
                break;
            case SL_FCGI_PARSER_STATE_ERROR:
            case SL_FCGI_PARSER_STATE_FINISHED:
                return total_parsed;
            default:
                parser->state = SL_FCGI_PARSER_STATE_ERROR;
                return total_parsed;
        }

        total_parsed ++;
    }

    return total_parsed;
}

void sl_fcgi_request_init(sl_fcgi_request *request, sl_arena *arena, sl_log *log, size_t param_hashtable_size)
{
    *request = (sl_fcgi_request) {0};

    request->state = SL_FCGI_REQUEST_STATE_BEGIN;
    request->arena = arena;
    request->log = log;

    sl_hashtable_init(&request->parameters, request->arena, param_hashtable_size, true);
}

static void sl_fcgi_request_append_param(sl_fcgi_request *request, sl_fcgi_parser *parser)
{
    for (sl_fcgi_msg_param *parameter = parser->first_param; parameter != NULL; parameter = parameter->next) {
        sl_string *name = sl_string_create_from_buffer(request->arena, (char *) parameter->name, parameter->name_length, parameter->name_length);
        sl_string *value = sl_string_create_from_buffer(request->arena, (char *) parameter->value, parameter->value_length, parameter->value_length);;
        sl_hashtable_set(&request->parameters, name, value);
    }
}

static void sl_fcgi_request_append_stdin(sl_fcgi_request *request, sl_fcgi_parser *parser)
{
    if (parser->stdin_stream.length == 0) {
        return;
    }

    if (sl_string_append_with_buffer(request->arena, &request->stdin, (char *) parser->stdin_stream.data, parser->stdin_stream.length) == -1) {
        request->state = SL_FCGI_REQUEST_STATE_ERROR;
        return;
    }
}

void sl_fcgi_request_process(sl_fcgi_request *request, sl_fcgi_parser *parser)
{
    if (parser->state != SL_FCGI_PARSER_STATE_FINISHED) {
        request->state = SL_FCGI_REQUEST_STATE_ERROR;
        return;
    }

    switch (request->state) {
        case SL_FCGI_REQUEST_STATE_BEGIN:
            if (parser->message_header.type != SL_FCGI_TYPE_BEGIN_REQUEST) {
                request->state = SL_FCGI_REQUEST_STATE_ERROR;
                break;
            }

            request->flags = parser->begin_message.flags;
            request->request_id = parser->message_header.request_id;
            request->state = SL_FCGI_REQUEST_STATE_PARAM_OR_STDIN;
            break;
        case SL_FCGI_REQUEST_STATE_PARAM_OR_STDIN:
            if (parser->message_header.type == SL_FCGI_TYPE_PARAMS) {
                if (parser->message_header.content_length == 0) {
                    request->state = SL_FCGI_REQUEST_STATE_STDIN;
                    break;
                }
                sl_fcgi_request_append_param(request, parser);
                break;
            } else if (parser->message_header.type == SL_FCGI_TYPE_STDIN) {
                if (parser->message_header.content_length == 0) {
                    request->state = SL_FCGI_REQUEST_STATE_PARAM;
                    break;
                }
                sl_fcgi_request_append_stdin(request, parser);
                break;
            }
            request->state = SL_FCGI_REQUEST_STATE_ERROR;
            break;
        case SL_FCGI_REQUEST_STATE_PARAM:
            if (parser->message_header.type != SL_FCGI_TYPE_PARAMS) {
                request->state = SL_FCGI_REQUEST_STATE_ERROR;
                break;
            }

            if (parser->message_header.content_length == 0) {
                request->state = SL_FCGI_REQUEST_STATE_FINISHED;
                break;
            }
            sl_fcgi_request_append_param(request, parser);
            break;
        case SL_FCGI_REQUEST_STATE_STDIN:
            if (parser->message_header.type != SL_FCGI_TYPE_STDIN) {
                request->state = SL_FCGI_REQUEST_STATE_ERROR;
                break;
            }

            if (parser->message_header.content_length == 0) {
                request->state = SL_FCGI_REQUEST_STATE_FINISHED;
                break;
            }
            sl_fcgi_request_append_stdin(request, parser);
            break;
        case SL_FCGI_REQUEST_STATE_FINISHED:
            request->state = SL_FCGI_REQUEST_STATE_ERROR;
            break;
        case SL_FCGI_REQUEST_STATE_ERROR:
            break;
    }
}

void sl_fcgi_response_init(sl_fcgi_response *response, sl_arena *arena, sl_log *log, size_t header_hashtable_size)
{
    *response = (sl_fcgi_response) {0};

    response->arena = arena;
    response->log = log;

    sl_hashtable_init(&response->headers, response->arena, header_hashtable_size, true);
}

inline int sl_fcgi_response_append_header(sl_fcgi_response *response, sl_string *name, sl_string *value)
{
    return sl_hashtable_set(&response->headers, name, value);
}

inline int sl_fcgi_response_append_output(sl_fcgi_response *response, sl_string *output)
{
    return sl_string_append_with_string(response->arena, &response->stdout, output);
}

sl_string *sl_fcgi_response_process(sl_fcgi_response *response)
{
    sl_string *output = sl_arena_allocate(response->arena, sizeof(sl_string));
    if (output == NULL) {
        return NULL;
    }

    *output = (sl_string) {0};

    for (size_t n = 0; n < response->headers.size; n ++) {
        sl_hashtable_bucket *bucket = response->headers.buckets[n];
        if (bucket == NULL) {
            continue;
        }

        for (sl_hashtable_bucket *node = bucket; node != NULL; node = node->next) {
            sl_string *header = sl_string_format(response->arena, "%S: %S\r\n", node->key, node->value);
            if (header == NULL) {
                return NULL;
            }
            if (sl_string_append_with_string(response->arena, output, header) == -1) {
                return NULL;
            }
        }
    }

    if (sl_string_append_with_buffer(response->arena, output, "\r\n", 2) == -1) {
        return NULL;
    }

    if (sl_string_append_with_string(response->arena, output, &response->stdout) == -1) {
        return NULL;
    }

    return output;
}
