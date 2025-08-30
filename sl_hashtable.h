#ifndef SL_HASHTABLE_H
#define SL_HASHTABLE_H

#include <stdlib.h>
#include <stdbool.h>

#include "sl_arena.h"
#include "sl_string.h"

#define SL_HASHTABLE_MAX_LOAD 75

typedef size_t sl_hashtable_hash;

typedef struct sl_hashtable_bucket sl_hashtable_bucket;
typedef struct sl_hashtable sl_hashtable;

struct sl_hashtable_bucket {
    sl_hashtable_bucket *next;
    sl_hashtable_hash hash;
    sl_string *key;
    sl_string *value;
};

struct sl_hashtable {
    sl_hashtable_bucket **buckets;
    sl_arena *arena;
    bool resize;
    size_t preallocate;
    size_t size;
    size_t count;
    size_t resizes;
    size_t wasted;
};

void sl_hashtable_init(sl_hashtable *hashtable, sl_arena *arena, size_t preallocate, bool resize);
sl_string *sl_hashtable_get(sl_hashtable *hashtable, sl_string *key);
int sl_hashtable_set(sl_hashtable *hashtable, sl_string *key, sl_string *value);

#endif
