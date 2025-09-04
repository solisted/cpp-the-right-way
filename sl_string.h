#ifndef SL_STRING_H
#define SL_STRING_H

#include <stdlib.h>
#include <stdarg.h>

#include "sl_arena.h"

typedef struct sl_string sl_string;

struct sl_string {
    size_t allocated;
    size_t length;
    char *buffer;
};

sl_string sl_string_init_with_buffer(char *buffer, size_t length);
sl_string sl_string_init_with_cstring(char *cstring);

sl_string *sl_string_create_from_buffer(sl_arena *arena, char *buffer, size_t length, size_t preallocate);
sl_string *sl_string_create_with_buffer(sl_arena *arena, char *buffer, size_t length);
int sl_string_append_with_buffer(sl_arena *arena, sl_string *string, char *buffer, size_t length);

sl_string *sl_string_create_from_string(sl_arena *arena, sl_string *string, size_t preallocate);
sl_string *sl_string_create_with_string(sl_arena *arena, sl_string *string);
int sl_string_append_with_string(sl_arena *arena, sl_string *destination, sl_string *source);

size_t sl_string_itoa(uintptr_t value, char *buffer, size_t length);
sl_string *sl_string_format(sl_arena *arena, char *format, ...);
sl_string *sl_string_format_buffer(sl_arena *arena, char *format, size_t length, va_list arguments);

#endif
