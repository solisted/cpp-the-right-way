#ifndef SL_STRING_H
#define SL_STRING_H

#include <stdlib.h>

typedef struct sl_string sl_string;

struct sl_string {
    size_t length;
    char *buffer;
};

#endif
