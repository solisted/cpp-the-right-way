#include "sl_string.h"

#include <string.h>

#define SL_STRING_64BIT_DEC_DIGITS 20
#define SL_STRING_64BIT_HEX_DIGITS 16

inline sl_string sl_string_init_with_buffer(char *buffer, size_t length)
{
    return (sl_string) {length, length, buffer};
}

inline sl_string sl_string_init_with_cstring(char *cstring)
{
    size_t length = strlen(cstring);

    return (sl_string) {length, length, cstring};
}

sl_string *sl_string_create_from_buffer(sl_arena *arena, char *buffer, size_t length, size_t preallocate)
{
    sl_string *string = sl_arena_allocate(arena, sizeof(sl_string));
    if (string == NULL) {
        return NULL;
    }

    size_t pow2_size = sl_arena_pow2_size(length <= preallocate ? preallocate : length << 1);

    string->buffer = sl_arena_allocate(arena, pow2_size);
    if (string->buffer == NULL) {
        return NULL;
    }

    memcpy(string->buffer, buffer, length);
    memset(string->buffer + length, 0, pow2_size - length);

    string->allocated = pow2_size;
    string->length = length;

    return string;
}

sl_string *sl_string_create_with_buffer(sl_arena *arena, char *buffer, size_t length)
{
    sl_string *string = sl_arena_allocate(arena, sizeof(sl_string));
    if (string == NULL) {
        return NULL;
    }

    string->buffer = buffer;
    string->allocated = length;
    string->length = length;

    return string;
}

int sl_string_append_with_buffer(sl_arena *arena, sl_string *string, char *buffer, size_t length)
{
    if (string->length + length <= string->allocated) {
        memcpy(string->buffer + string->length, buffer, length);
        string->length += length;

        return 0;
    }

    size_t pow2_size = sl_arena_pow2_size((string->length + length) << 1);
    char *new_buffer = sl_arena_allocate(arena, pow2_size);
    if (new_buffer == NULL) {
        return -1;
    }

    memcpy(new_buffer, string->buffer, string->length);
    memcpy(new_buffer + string->length, buffer, length);
    memset(new_buffer + string->length + length, 0, pow2_size - string->length - length);

    string->buffer = new_buffer;
    string->allocated = pow2_size;
    string->length += length;

    return 0;
}

inline sl_string *sl_string_create_from_string(sl_arena *arena, sl_string *string, size_t preallocate)
{
    return sl_string_create_from_buffer(arena, string->buffer, string->length, preallocate);
}

inline sl_string *sl_string_create_with_string(sl_arena *arena, sl_string *string)
{
    return sl_string_create_with_buffer(arena, string->buffer, string->length);
}

inline int sl_string_append_with_string(sl_arena *arena, sl_string *destination, sl_string *source)
{
    return sl_string_append_with_buffer(arena, destination, source->buffer, source->length);
}

size_t sl_string_itoa(uintptr_t value, char *buffer, size_t length)
{
    size_t value_length = 0;

    while (value > 0 && length-- > 0) {
        uint8_t digit = '0' + (value % 10);
        buffer[length] = digit;
        value /= 10;
        value_length ++;
    }

    for (size_t n = 0; n < value_length; n ++) {
        buffer[n] = buffer[n + length];
    }

    return value_length;
}

sl_string *sl_string_format(sl_arena *arena, char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    sl_string *result = sl_string_format_buffer(arena, format, strlen(format), arguments);
    va_end(arguments);

    return result;
}

sl_string *sl_string_format_buffer(sl_arena *arena, char *format, size_t length, va_list arguments)
{
    sl_string *string = sl_string_create_from_buffer(arena, "", 0, length << 1);
    if (string == NULL) {
        return NULL;
    }

    while (length-- > 0) {
        if (*format != '%') {
            string->buffer[string->length++] = *format++;
            continue;
        }


        switch (*(++format)) {
            case 'z':
                char buffer[SL_STRING_64BIT_DEC_DIGITS];

                size_t arg_size = va_arg(arguments, size_t);
                size_t arg_length = sl_string_itoa(arg_size, buffer, sizeof(buffer));

                if (sl_string_append_with_buffer(arena, string, buffer, arg_length) == -1) {
                    return NULL;
                }
                break;
            case 's':
                char *arg_cstring = va_arg(arguments, char *);
                if (arg_cstring == NULL) {
                    break;
                }

                if (sl_string_append_with_buffer(arena, string, arg_cstring, strlen(arg_cstring)) == -1) {
                    return NULL;
                }
                break;
            case 'S':
                sl_string *arg_string = va_arg(arguments, sl_string*);
                if (arg_string == NULL) {
                    break;
                }

                if (sl_string_append_with_string(arena, string, arg_string) == -1) {
                    return NULL;
                }
                break;
            default:
                break;
        }

        format ++;
        length --;
    }

    return string;
}
