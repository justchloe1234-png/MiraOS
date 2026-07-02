#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Dynamic array
typedef struct {
    void *data;
    size_t elem_size;
    size_t count;
    size_t capacity;
} ds_array_t;

ds_array_t *ds_array_create(size_t elem_size, size_t initial_capacity);
void ds_array_destroy(ds_array_t *arr);
void ds_array_push(ds_array_t *arr, const void *elem);
void *ds_array_get(ds_array_t *arr, size_t index);
void ds_array_remove(ds_array_t *arr, size_t index);
size_t ds_array_count(ds_array_t *arr);

// Linked list
typedef struct ds_list_node {
    void *data;
    struct ds_list_node *next;
} ds_list_node_t;

typedef struct {
    ds_list_node_t *head;
    ds_list_node_t *tail;
    size_t count;
} ds_list_t;

ds_list_t *ds_list_create(void);
void ds_list_destroy(ds_list_t *list);
void ds_list_push(ds_list_t *list, void *data);
void *ds_list_pop(ds_list_t *list);
void *ds_list_get(ds_list_t *list, size_t index);
void ds_list_remove(ds_list_t *list, size_t index);

// Hash map
typedef struct {
    void *keys;
    void *values;
    size_t key_size;
    size_t value_size;
    size_t capacity;
    size_t count;
} ds_hashmap_t;

ds_hashmap_t *ds_hashmap_create(size_t key_size, size_t value_size, size_t capacity);
void ds_hashmap_destroy(ds_hashmap_t *map);
bool ds_hashmap_set(ds_hashmap_t *map, const void *key, const void *value);
void *ds_hashmap_get(ds_hashmap_t *map, const void *key);
bool ds_hashmap_remove(ds_hashmap_t *map, const void *key);

// String utilities
size_t ds_strlen(const char *str);
char *ds_strdup(const char *str);
int ds_strcmp(const char *a, const char *b);
char *ds_strcpy(char *dst, const char *src);
char *ds_strcat(char *dst, const char *src);