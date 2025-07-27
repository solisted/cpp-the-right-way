#include "sl_fcgi.h"

#include <stdio.h>

void sl_fcgi_parser_init(sl_fcgi_parser *parser, sl_arena *arena)
{
    memset(parser, 0, sizeof(sl_fcgi_parser));

    parser->state = SL_FGI_PARSER_STATE_VERSION;
    parser->arena = arena;
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
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_FLAGS;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_FLAGS:
            parser->begin_message.flags = octet;
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP5;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP5:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP4;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP4:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP3;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP3:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP2;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP2:
            parser->state = SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP1;
            break;
        case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP1:
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
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_FLAGS:
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP5:
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP4:
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP3:
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP2:
            case SL_FGI_PARSER_STATE_BEGIN_ROLE_SKIP1:
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
