#include "ds.h"
#include "kernel/heap.h"

ds_array_t *ds_array_create(size_t elem_size, size_t initial_capacity) {
    ds_array_t *arr = (ds_array_t *)kmalloc(sizeof(ds_array_t));
    if (!arr)
        return 0;
    arr->data = kmalloc(elem_size * initial_capacity);
    if (!arr->data) {
        kfree(arr);
        return 0;
    }
    arr->elem_size = elem_size;
    arr->count = 0;
    arr->capacity = initial_capacity;
    return arr;
}

void ds_array_destroy(ds_array_t *arr) {
    if (!arr)
        return;
    if (arr->data)
        kfree(arr->data);
    kfree(arr);
}

void ds_array_push(ds_array_t *arr, const void *elem) {
    if (!arr || !elem)
        return;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        void *new_data = kmalloc(arr->elem_size * new_cap);
        if (!new_data)
            return;
        for (size_t i = 0; i < arr->count * arr->elem_size; i++)
            ((uint8_t *)new_data)[i] = ((uint8_t *)arr->data)[i];
        kfree(arr->data);
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    for (size_t i = 0; i < arr->elem_size; i++)
        ((uint8_t *)arr->data)[arr->count * arr->elem_size + i] = ((const uint8_t *)elem)[i];
    arr->count++;
}

void *ds_array_get(ds_array_t *arr, size_t index) {
    if (!arr || index >= arr->count)
        return 0;
    return (uint8_t *)arr->data + index * arr->elem_size;
}

void ds_array_remove(ds_array_t *arr, size_t index) {
    if (!arr || index >= arr->count)
        return;
    for (size_t i = index; i < arr->count - 1; i++) {
        for (size_t j = 0; j < arr->elem_size; j++)
            ((uint8_t *)arr->data)[i * arr->elem_size + j] = ((uint8_t *)arr->data)[(i + 1) * arr->elem_size + j];
    }
    arr->count--;
}

size_t ds_array_count(ds_array_t *arr) {
    return arr ? arr->count : 0;
}

ds_list_t *ds_list_create(void) {
    ds_list_t *list = (ds_list_t *)kmalloc(sizeof(ds_list_t));
    if (!list)
        return 0;
    list->head = 0;
    list->tail = 0;
    list->count = 0;
    return list;
}

void ds_list_destroy(ds_list_t *list) {
    if (!list)
        return;
    ds_list_node_t *node = list->head;
    while (node) {
        ds_list_node_t *next = node->next;
        kfree(node->data);
        kfree(node);
        node = next;
    }
    kfree(list);
}

void ds_list_push(ds_list_t *list, void *data) {
    if (!list || !data)
        return;
    ds_list_node_t *node = (ds_list_node_t *)kmalloc(sizeof(ds_list_node_t));
    if (!node)
        return;
    node->data = data;
    node->next = 0;
    if (!list->tail) {
        list->head = node;
        list->tail = node;
    } else {
        list->tail->next = node;
        list->tail = node;
    }
    list->count++;
}

void *ds_list_pop(ds_list_t *list) {
    if (!list || !list->head)
        return 0;
    ds_list_node_t *node = list->head;
    void *data = node->data;
    list->head = node->next;
    if (!list->head)
        list->tail = 0;
    kfree(node);
    list->count--;
    return data;
}

void *ds_list_get(ds_list_t *list, size_t index) {
    if (!list || index >= list->count)
        return 0;
    ds_list_node_t *node = list->head;
    for (size_t i = 0; i < index; i++)
        node = node->next;
    return node->data;
}

void ds_list_remove(ds_list_t *list, size_t index) {
    if (!list || index >= list->count)
        return;
    ds_list_node_t *prev = 0;
    ds_list_node_t *node = list->head;
    for (size_t i = 0; i < index; i++) {
        prev = node;
        node = node->next;
    }
    if (prev)
        prev->next = node->next;
    else
        list->head = node->next;
    if (node == list->tail)
        list->tail = prev;
    kfree(node->data);
    kfree(node);
    list->count--;
}

ds_hashmap_t *ds_hashmap_create(size_t key_size, size_t value_size, size_t capacity) {
    ds_hashmap_t *map = (ds_hashmap_t *)kmalloc(sizeof(ds_hashmap_t));
    if (!map)
        return 0;
    map->keys = kmalloc(key_size * capacity);
    map->values = kmalloc(value_size * capacity);
    if (!map->keys || !map->values) {
        if (map->keys) kfree(map->keys);
        if (map->values) kfree(map->values);
        kfree(map);
        return 0;
    }
    map->key_size = key_size;
    map->value_size = value_size;
    map->capacity = capacity;
    map->count = 0;
    return map;
}

void ds_hashmap_destroy(ds_hashmap_t *map) {
    if (!map)
        return;
    kfree(map->keys);
    kfree(map->values);
    kfree(map);
}

bool ds_hashmap_set(ds_hashmap_t *map, const void *key, const void *value) {
    if (!map || !key || !value || map->count >= map->capacity)
        return false;
    for (size_t i = 0; i < map->key_size; i++)
        ((uint8_t *)map->keys)[map->count * map->key_size + i] = ((const uint8_t *)key)[i];
    for (size_t i = 0; i < map->value_size; i++)
        ((uint8_t *)map->values)[map->count * map->value_size + i] = ((const uint8_t *)value)[i];
    map->count++;
    return true;
}

void *ds_hashmap_get(ds_hashmap_t *map, const void *key) {
    if (!map || !key)
        return 0;
    for (size_t i = 0; i < map->count; i++) {
        bool match = true;
        for (size_t j = 0; j < map->key_size; j++) {
            if (((uint8_t *)map->keys)[i * map->key_size + j] != ((const uint8_t *)key)[j]) {
                match = false;
                break;
            }
        }
        if (match)
            return (uint8_t *)map->values + i * map->value_size;
    }
    return 0;
}

bool ds_hashmap_remove(ds_hashmap_t *map, const void *key) {
    if (!map || !key)
        return false;
    for (size_t i = 0; i < map->count; i++) {
        bool match = true;
        for (size_t j = 0; j < map->key_size; j++) {
            if (((uint8_t *)map->keys)[i * map->key_size + j] != ((const uint8_t *)key)[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            for (size_t k = i; k < map->count - 1; k++) {
                for (size_t j = 0; j < map->key_size; j++)
                    ((uint8_t *)map->keys)[k * map->key_size + j] = ((uint8_t *)map->keys)[(k + 1) * map->key_size + j];
                for (size_t j = 0; j < map->value_size; j++)
                    ((uint8_t *)map->values)[k * map->value_size + j] = ((uint8_t *)map->values)[(k + 1) * map->value_size + j];
            }
            map->count--;
            return true;
        }
    }
    return false;
}

size_t ds_strlen(const char *str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

char *ds_strdup(const char *str) {
    size_t len = ds_strlen(str);
    char *dup = (char *)kmalloc(len + 1);
    if (!dup)
        return 0;
    for (size_t i = 0; i <= len; i++)
        dup[i] = str[i];
    return dup;
}

int ds_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }
    return *a - *b;
}

char *ds_strcpy(char *dst, const char *src) {
    char *orig = dst;
    while (*src) {
        *dst = *src;
        dst++;
        src++;
    }
    *dst = 0;
    return orig;
}

char *ds_strcat(char *dst, const char *src) {
    char *orig = dst;
    while (*dst)
        dst++;
    while (*src) {
        *dst = *src;
        dst++;
        src++;
    }
    *dst = 0;
    return orig;
}