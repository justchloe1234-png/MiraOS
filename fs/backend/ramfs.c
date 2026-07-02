#include "ramfs.h"
#include "vfs.h"
#include "kernel/heap.h"

static vfs_node_t ramfs_root_node;

void ramfs_init(void) {
    for (size_t i = 0; i < sizeof(vfs_node_t); i++)
        ((uint8_t *)&ramfs_root_node)[i] = 0;

    ramfs_root_node.name[0] = '/';
    ramfs_root_node.name[1] = 0;
    ramfs_root_node.type = VFS_TYPE_DIR;
    ramfs_root_node.ops = 0;

    vfs_init(&ramfs_root_node);

    const char *msg = "MiraOS ramfs online";
    vfs_create_file("/readme.txt", msg, 19);

    const char *ver = "1.0";
    vfs_create_file("/version", ver, 3);
}

vfs_node_t *ramfs_root(void) {
    return &ramfs_root_node;
}
