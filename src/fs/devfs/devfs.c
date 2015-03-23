/*
 *  Andromeda
 *  Copyright (C) 2015 Bart Kuivenhoven
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <fs/vfs.h>
#include <andromeda/system.h>
#include <andromeda/drivers.h>
#include <types.h>
#include <lib/tree.h>
#include <fs/path.h>

struct devfs_device_node {
        char name[FS_MAX_NAMELEN];
        union {
                struct device* device;
                struct tree_root* directory;
        };
        enum {
                DEVFS_NODE_DIRECTORY, DEVFS_NODE_DEVICE
        } node_type;
};

struct devfs_device_node devfs_root;
static int devfs_initialised = 0;
static mutex_t devfs_lock = mutex_unlocked;

static int fs_devfs_open(struct vfile* this, char* path, size_t strlen)
{
        return -E_NOFUNCTION;
}

static int fs_devfs_close(struct vfile* this)
{
        return -E_NOFUNCTION;
}

static size_t fs_devfs_write(struct vfile* file, char* buf, size_t start,
                size_t len)
{
        return 0;
}

static size_t fs_devfs_read(struct vfile* file, char* buf, size_t start,
                size_t len)
{
        return 0;
}

static struct devfs_device_node* devfs_make_directory(char* name)
{
        struct devfs_device_node* node = kmalloc(sizeof(*node));
        if (node == NULL) {
                return NULL;
        }

        node->node_type = DEVFS_NODE_DIRECTORY;
        node->directory = tree_new_string_avl();
        if (node->directory == NULL) {
                kfree_s(node, sizeof(*node));
                return NULL;
        }
        memcpy(node->name, name, FS_MAX_NAMELEN);

        return node;
}

int fs_devfs_register(char* path, struct device* device)
{
        struct path_directory_node* nodes = parse_path(path);

        int success = -E_SUCCESS;
        if (nodes == NULL) {
                return -E_INVALID_ARG;
        }

        if (device == NULL) {
                return -E_NULL_PTR;
        }

        struct devfs_device_node* d = kmalloc(sizeof(*d));
        if (d == NULL) {
                return -E_NOMEM;
        }
        memset(d, 0, sizeof(*d));
        memcpy(d->name, device->name, FS_MAX_NAMELEN);
        d->node_type = DEVFS_NODE_DEVICE;
        d->device = device;

        mutex_lock(&devfs_lock);
        struct path_directory_node* p = nodes;

        struct devfs_device_node* dev = &devfs_root;
        struct devfs_device_node* next;
        for (; p != NULL ; p = p->next) {
                if (dev->node_type != DEVFS_NODE_DIRECTORY) {
                        /* Went into node that is not a directory ... error */
                        success = -E_CORRUPT;
                        break;
                }
                if (dev->directory == NULL) {
                        dev->directory = tree_new_string_avl();
                        if (dev->directory == NULL) {
                                /* Out of memory! Return */
                                success = -E_NOMEM;
                                break;
                        }
                }
                /* Find next node */
                next = dev->directory->string_find(&p->name, dev->directory);
                if (next != NULL) {
                        /* Found it, move on! */
                        dev = next;
                        continue;
                }
                /* Not found */
                if (p->next == NULL) {
                        /* We're at the end of the list, let's add the
                         * device here! */
                        dev->directory->string_add(p->name, d, dev->directory);
                        success = -E_SUCCESS;
                        break;
                } else {
                        /* We're not yet at the end of the path, let's create
                         * the directory required! */
                        next = devfs_make_directory(p->name);
                        if (next == NULL) {
                                success = -E_NOMEM;
                                break;
                        }
                        dev->directory->add(p->name, next, dev->directory);
                        dev = next;
                }
        }

        /* Return the memory used by the path */
        mutex_unlock(&devfs_lock);
        clean_path(nodes);
        return success;
}

int fs_devfs_init()
{
        if ((devfs_initialised++) != 0) {
                panic("Trying to reinitialise the device filesystem!");
        }

        memset(&devfs_root, 0, sizeof(devfs_root));
        devfs_root.node_type = DEVFS_NODE_DIRECTORY;
        memset(devfs_root.name, 0, sizeof(devfs_root.name));
        devfs_root.name[0] = '/';

        devfs_root.directory = tree_new_string_avl();
        if (devfs_root.directory == NULL) {
                panic("Could not initialise the device filesystem");
        }
        memset(devfs_root.directory, 0, sizeof(*devfs_root.directory));

        return -E_NOFUNCTION;
}
