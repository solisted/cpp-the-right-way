#ifndef SL_ARENA_H
#define SL_ARENA_H

#include <stdint.h>
#include <unistd.h>

typedef struct sl_arena_block sl_arena_block;
typedef struct sl_arena sl_arena;

struct sl_arena_block {
    sl_arena_block *next;
    size_t allocated;
    size_t used;
    uint8_t buffer[];
};

struct sl_arena {
    sl_arena_block *first;
    sl_arena_block *last;
    size_t preallocate;
    size_t allocations;
    size_t allocated;
    size_t blocks;
    size_t used;
};

void sl_arena_init(sl_arena *arena, size_t preallocate);
void sl_arena_rewind(sl_arena *arena);
void sl_arena_destroy(sl_arena *arena);
void *sl_arena_allocate(sl_arena *arena, size_t size);

#endif
