#include "sl_arena.h"

#include <stdlib.h>

size_t sl_arena_pow2_size(size_t size)
{
    size --;
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    size |= size >> 32;
    size ++;

    return size;
}

static sl_arena_block *sl_arena_create_block(size_t size)
{
    sl_arena_block *block = malloc(sizeof(sl_arena_block) + size);
    if (block == NULL) {
        return NULL;
    }

    block->next = NULL;
    block->allocated = size;
    block->used = 0;

    return block;
}

static void *sl_arena_allocate_from_block(sl_arena_block *block, size_t size)
{
    if (size > block->allocated - block->used) {
        return NULL;
    }

    void *buffer = block->buffer + block->used;
    block->used += size;

    return buffer;
}

void sl_arena_init(sl_arena *arena, size_t preallocate)
{
    *arena = (sl_arena) {0};

    arena->preallocate = preallocate;
}

void sl_arena_rewind(sl_arena *arena)
{
    for (sl_arena_block *block = arena->first; block != NULL; block = block->next) {
        block->used = 0;
    }
}

void sl_arena_destroy(sl_arena *arena)
{
    sl_arena_block *previous = NULL;

    for (sl_arena_block *block = arena->first; block != NULL; block = block->next) {
        if (previous != NULL) {
            free(previous);
        }

        previous = block;
    }

    free(previous);

    sl_arena_init(arena, arena->preallocate);
}

void *sl_arena_allocate(sl_arena *arena, size_t size)
{
    sl_arena_block *block = NULL;
    void *buffer = NULL;

    for (block = arena->first; block != NULL; block = block->next) {
        if ((buffer = sl_arena_allocate_from_block(block, size)) != NULL) {
            arena->allocations ++;
            arena->used += size;
            return buffer;
        }
    }

    block = sl_arena_create_block(size > (arena->preallocate >> 1) ? size << 1 : arena->preallocate);
    if (block == NULL) {
        return NULL;
    }

    if (arena->last != NULL) {
        arena->last->next = block;
    }

    arena->last = block;
    arena->blocks ++;
    arena->allocated += block->allocated;

    if (arena->first == NULL) {
        arena->first = arena->last;
    }

    buffer = sl_arena_allocate_from_block(block, size);
    if (buffer == NULL) {
        return NULL;
    }

    arena->allocations ++;
    arena->used +=size;

    return buffer;
}
