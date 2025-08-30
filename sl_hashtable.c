#include "sl_hashtable.h"

static bool sl_hashtable_are_keys_equal(sl_string *key_a, sl_string *key_b)
{
    if (key_a->length != key_b->length) {
        return false;
    }

    for (size_t n = 0; n < key_a->length; n ++) {
        if (key_a->buffer[n] != key_b->buffer[n]) {
            return false;
        }
    }

    return true;
}

static sl_hashtable_hash sl_hashtable_compute_hash(sl_string *key)
{
    sl_hashtable_hash hash = 0;

    for (size_t n = 0; n < key->length; n ++) {
        hash = key->buffer[n] + (hash << 5) - hash;
    }

    return hash;
}

static sl_hashtable_bucket *sl_hashtable_create_bucket(sl_arena *arena, sl_hashtable_hash hash, sl_string *key, sl_string *value)
{
    sl_hashtable_bucket *bucket = sl_arena_allocate(arena, sizeof(sl_hashtable_bucket));
    if (bucket == NULL) {
        return NULL;
    }

    bucket->next = NULL;
    bucket->hash = hash;
    bucket->key = key;
    bucket->value = value;

    return bucket;
}

static int sl_hashtable_allocate(sl_hashtable *hashtable, size_t size)
{
    size_t new_pow2_size = sl_arena_pow2_size(size);
    size_t new_size = new_pow2_size < hashtable->preallocate ? hashtable->preallocate : new_pow2_size;

    sl_hashtable_bucket **buckets = sl_arena_allocate(hashtable->arena, sizeof(sl_hashtable_bucket*) * new_size);
    if (buckets == NULL) {
        return -1;
    }

    hashtable->buckets = buckets;
    hashtable->size = new_size;
    hashtable->count = 0;

    for (size_t n = 0; n < hashtable->size; n ++) {
        hashtable->buckets[n] = NULL;
    }

    return 0;
}

static int sl_hashtable_resize(sl_hashtable *hashtable, size_t size)
{
    size_t new_pow2_size = sl_arena_pow2_size(size);
    if (new_pow2_size <= hashtable->size) {
        return 0;
    }

    sl_hashtable_bucket **new_buckets = sl_arena_allocate(hashtable->arena, sizeof(sl_hashtable_bucket*) * new_pow2_size);
    if (new_buckets == NULL) {
        return -1;
    }

    for (size_t n = 0; n < new_pow2_size; n ++) {
        new_buckets[n] = NULL;
    }

    for (size_t n = 0; n < hashtable->size; n ++) {
        sl_hashtable_bucket *bucket = hashtable->buckets[n];
        if (bucket == NULL) {
            continue;
        }

        for (sl_hashtable_bucket *node = bucket; node != NULL; node = node->next) {
            sl_hashtable_hash original_hash = node->hash;
            sl_hashtable_hash hash = original_hash & (new_pow2_size - 1);

            sl_hashtable_bucket *new_bucket = new_buckets[hash];
            if (new_bucket == NULL) {
                new_buckets[hash] = sl_hashtable_create_bucket(hashtable->arena, node->hash, node->key, node->value);
                if (new_buckets[hash] == NULL) {
                    return -1;
                }

                hashtable->wasted += sizeof(sl_hashtable_bucket);
                continue;
            }

            sl_hashtable_bucket *new_node;
            for (new_node = new_bucket; new_node->next != NULL; new_node = new_node->next);

            new_node->next = sl_hashtable_create_bucket(hashtable->arena, node->hash, node->key, node->value);
            if (new_node->next == NULL) {
                return -1;
            }

            hashtable->wasted += sizeof(sl_hashtable_bucket);
        }
    }

    hashtable->resizes ++;
    hashtable->wasted += hashtable->size * sizeof(sl_hashtable_bucket*);
    hashtable->buckets = new_buckets;
    hashtable->size = new_pow2_size;

    return 0;
}

static bool sl_hashtable_needs_resize(sl_hashtable *hashtable) {
    return hashtable->resize == true && hashtable->count * 100 / hashtable->size >= SL_HASHTABLE_MAX_LOAD;
}

void sl_hashtable_init(sl_hashtable *hashtable, sl_arena *arena, size_t preallocate, bool resize)
{
    *hashtable = (sl_hashtable) {0};

    hashtable->arena = arena;
    hashtable->preallocate = sl_arena_pow2_size(preallocate);
    hashtable->resize = resize;
}

sl_string *sl_hashtable_get(sl_hashtable *hashtable, sl_string *key)
{
    sl_hashtable_hash original_hash = sl_hashtable_compute_hash(key);

    for (size_t n = 0; n < hashtable->size; n ++) {
        sl_hashtable_bucket *bucket = hashtable->buckets[n];
        if (bucket == NULL) {
            continue;
        }

        for (sl_hashtable_bucket *node = bucket; node != NULL; node = node->next) {
            if (node->hash == original_hash && sl_hashtable_are_keys_equal(node->key, key) == true) {

                return node->value;
            }
        }
    }

    return NULL;
}

int sl_hashtable_set(sl_hashtable *hashtable, sl_string *key, sl_string *value)
{
    if (hashtable->size == 0 && sl_hashtable_allocate(hashtable, hashtable->preallocate) == -1) {
        return -1;
    }

    sl_hashtable_hash original_hash = sl_hashtable_compute_hash(key);
    sl_hashtable_hash hash = original_hash & (hashtable->size - 1);

    sl_hashtable_bucket *bucket = hashtable->buckets[hash];
    if (bucket == NULL) {
        bucket = sl_hashtable_create_bucket(hashtable->arena, original_hash, key, value);
        if (bucket == NULL) {
            return -1;
        }

        hashtable->buckets[hash] = bucket;
        hashtable->count ++;

        if (sl_hashtable_needs_resize(hashtable) && sl_hashtable_resize(hashtable, hashtable->size << 1) == -1) {
            return -1;
        }

        return 0;
    }

    sl_hashtable_bucket *last_bucket = bucket;

    for (sl_hashtable_bucket *node = bucket; node != NULL; node = node->next) {
        last_bucket = node;
        if (node->hash != original_hash || sl_hashtable_are_keys_equal(node->key, key) == false) {
            continue;
        }

        node->key = key;
        node->value = value;

        return 0;
    }

    last_bucket->next = sl_hashtable_create_bucket(hashtable->arena, original_hash, key, value);
    if (last_bucket->next == NULL) {
        return -1;
    }

    hashtable->count ++;

    if (sl_hashtable_needs_resize(hashtable) && sl_hashtable_resize(hashtable, hashtable->size << 1) == -1) {
        return -1;
    }

    return 0;
}
