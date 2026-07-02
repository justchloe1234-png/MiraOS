#pragma once

#include "fs/vfs.h"

void ramfs_init(void);
vfs_node_t *ramfs_root(void);
