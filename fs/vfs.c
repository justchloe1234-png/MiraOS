#include "vfs.h"
#include "kernel/heap.h"
#include "lib/common/mem.h"

static vfs_node_t *root_node;

static struct {
    vfs_node_t *node;
    size_t offset;
    int flags;
    int used;
} open_table[VFS_MAX_OPEN];

static vfs_ops_t default_ops;

static int vfs_default_open(vfs_node_t *node, int flags) {
    (void)node;
    (void)flags;
    return 0;
}

static int vfs_default_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static ssize_t vfs_default_read(vfs_node_t *node, void *buf, size_t count, size_t offset) {
    if (!node->data || offset >= node->size)
        return 0;
    if (offset + count > node->size)
        count = node->size - offset;
    uint8_t *src = (uint8_t *)node->data + offset;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
        dst[i] = src[i];
    return (ssize_t)count;
}

static ssize_t vfs_default_write(vfs_node_t *node, const void *buf, size_t count, size_t offset) {
    if (count == 0)
        return 0;
    size_t new_size = offset + count;
    if (new_size > node->size) {
        void *new_data = kmalloc(new_size);
        if (!new_data)
            return -1;
        uint8_t *nd = (uint8_t *)new_data;
        for (size_t i = 0; i < new_size; i++)
            nd[i] = 0;
        if (node->data && node->size > 0) {
            uint8_t *src = (uint8_t *)node->data;
            for (size_t i = 0; i < node->size; i++)
                nd[i] = src[i];
            kfree(node->data);
        }
        node->data = new_data;
        node->size = new_size;
    }
    uint8_t *dst = (uint8_t *)node->data + offset;
    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
        dst[i] = src[i];
    return (ssize_t)count;
}

void vfs_init(vfs_node_t *root) {
    root_node = root;
    default_ops.open = vfs_default_open;
    default_ops.close = vfs_default_close;
    default_ops.read = vfs_default_read;
    default_ops.write = vfs_default_write;
    for (int i = 0; i < VFS_MAX_OPEN; i++)
        open_table[i].used = 0;
}

vfs_node_t *vfs_root(void) {
    return root_node;
}

static vfs_node_t *vfs_find_child(vfs_node_t *dir, const char *name) {
    vfs_node_t *child = dir->children;
    while (child) {
        int match = 1;
        for (int i = 0; name[i] || child->name[i]; i++) {
            if (name[i] != child->name[i]) {
                match = 0;
                break;
            }
        }
        if (match)
            return child;
        child = child->next_sibling;
    }
    return 0;
}

vfs_node_t *vfs_lookup(const char *path) {
    if (!root_node || !path)
        return 0;
    if (path[0] == '/' && path[1] == 0)
        return root_node;

    vfs_node_t *current = root_node;
    const char *p = path;
    if (*p == '/')
        p++;

    char component[64];
    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < 63)
            component[i++] = *p++;
        component[i] = 0;
        if (*p == '/')
            p++;
        if (component[0] == 0)
            continue;
        current = vfs_find_child(current, component);
        if (!current)
            return 0;
    }
    return current;
}

int vfs_open(const char *path, int flags) {
    vfs_node_t *node = vfs_lookup(path);
    if (!node) {
        if (flags & VFS_O_CREAT) {
            vfs_create_file(path, 0, 0);
            node = vfs_lookup(path);
        }
        if (!node)
            return -1;
    }
    if (node->type == VFS_TYPE_DIR)
        return -1;

    for (int i = 0; i < VFS_MAX_OPEN; i++) {
        if (!open_table[i].used) {
            open_table[i].used = 1;
            open_table[i].node = node;
            open_table[i].offset = 0;
            open_table[i].flags = flags;
            if (node->ops && node->ops->open)
                node->ops->open(node, flags);
            return i;
        }
    }
    return -1;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !open_table[fd].used)
        return -1;
    vfs_node_t *node = open_table[fd].node;
    if (node->ops && node->ops->close)
        node->ops->close(node);
    open_table[fd].used = 0;
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !open_table[fd].used)
        return -1;
    vfs_node_t *node = open_table[fd].node;
    vfs_ops_t *ops = node->ops ? node->ops : &default_ops;
    ssize_t n = ops->read(node, buf, count, open_table[fd].offset);
    if (n > 0)
        open_table[fd].offset += (size_t)n;
    return n;
}

ssize_t vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !open_table[fd].used)
        return -1;
    vfs_node_t *node = open_table[fd].node;
    vfs_ops_t *ops = node->ops ? node->ops : &default_ops;
    ssize_t n = ops->write(node, buf, count, open_table[fd].offset);
    if (n > 0)
        open_table[fd].offset += (size_t)n;
    return n;
}

int vfs_lseek(int fd, size_t offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !open_table[fd].used)
        return -1;
    
    vfs_node_t *node = open_table[fd].node;
    size_t new_offset;
    
    switch (whence) {
        case VFS_SEEK_SET:
            new_offset = offset;
            break;
        case VFS_SEEK_CUR:
            new_offset = open_table[fd].offset + offset;
            break;
        case VFS_SEEK_END:
            new_offset = node->size + offset;
            break;
        default:
            return -1;
    }
    
    if (new_offset > node->size)
        return -1;
    
    open_table[fd].offset = new_offset;
    return 0;
}

static void vfs_set_name(vfs_node_t *node, const char *name) {
    int i = 0;
    while (name[i] && i < 63) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = 0;
}

static void vfs_add_child(vfs_node_t *parent, vfs_node_t *child) {
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    parent->child_count++;
}

vfs_node_t *vfs_create_file(const char *path, const void *data, size_t size) {
    char buf[64];
    int bi = 0;
    const char *p = path;
    if (*p == '/')
        p++;
    vfs_node_t *current = root_node;
    while (*p) {
        bi = 0;
        while (*p && *p != '/' && bi < 63)
            buf[bi++] = *p++;
        buf[bi] = 0;
        if (*p == '/')
            p++;
        if (buf[0] == 0)
            continue;
        vfs_node_t *child = vfs_find_child(current, buf);
        if (!child) {
            child = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
            if (!child)
                return 0;
            for (size_t i = 0; i < sizeof(vfs_node_t); i++)
                ((uint8_t *)child)[i] = 0;
            vfs_set_name(child, buf);
            if (*p == 0) {
                child->type = VFS_TYPE_FILE;
                child->size = size;
                if (size > 0) {
                    child->data = kmalloc(size);
                    if (!child->data) {
                        kfree(child);
                        return 0;
                    }
                    const uint8_t *src = (const uint8_t *)data;
                    uint8_t *dst = (uint8_t *)child->data;
                    for (size_t i = 0; i < size; i++)
                        dst[i] = src[i];
                } else {
                    child->data = 0;
                }
            } else {
                child->type = VFS_TYPE_DIR;
            }
            child->ops = &default_ops;
            vfs_add_child(current, child);
        }
        current = child;
    }
    return current;
}
