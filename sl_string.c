#include "sl_string.h"

#include <string.h>

sl_string *sl_string_create_from_buffer(sl_arena *arena, char *buffer, size_t length, size_t preallocate)
{
    sl_string *string = sl_arena_allocate(arena, sizeof(sl_string));
    if (string == NULL) {
        return NULL;
    }

    size_t pow2_size = sl_arena_pow2_size(length < preallocate ? preallocate : length << 1);

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

sl_string *sl_string_create_from_string(sl_arena *arena, sl_string *string, size_t preallocate)
{
    return sl_string_create_from_buffer(arena, string->buffer, string->length, preallocate);
}

sl_string *sl_string_create_with_string(sl_arena *arena, sl_string *string)
{
    return sl_string_create_with_buffer(arena, string->buffer, string->length);
}

int sl_string_append_with_string(sl_arena *arena, sl_string *destination, sl_string *source)
{
    return sl_string_append_with_buffer(arena, destination, source->buffer, source->length);
}
