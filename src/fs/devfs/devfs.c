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

#define DEVFS_DIRNODE_SIZE 255
#define DEVFS_NODE_DIRECTORY 0
#define DEVFS_NODE_DEVICE 1

struct devfs_device_node;

struct devfs_device_directory {
        struct devfs_device_directory* next;
        struct devfs_device_node* devices[DEVFS_DIRNODE_SIZE];
};

struct devfs_device_node {
        char name[FS_MAX_NAMELEN];
        union {
                struct device* device;
                struct devfs_device_directory* directory;
        };
        int node_type;
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

static int devfs_put_dir_entry(struct devfs_device_node* directory,
                struct devfs_device_node* file)
{
        /**
         * \warning This function doesn't check if the file already exists,
         * beware of that!
         */

        /* Check parameters */
        if (directory == NULL || file == NULL) {
                return -E_NULL_PTR;
        }

        /* Make sure we're working with a directory */
        if (directory->node_type != DEVFS_NODE_DIRECTORY) {
                return -E_INVALID_ARG;
        }

        /* Set up the loop paramters */
        struct devfs_device_directory* dir = directory->directory;
        struct devfs_device_directory* last = dir;
        /* If NULL, return */
        if (dir == NULL) {
                return -E_NULL_PTR;
        }

        /* While not at end of tables */
        while (dir != NULL ) {
                int idx = 0;
                for (; idx < DEVFS_DIRNODE_SIZE; idx++) {
                        /* If this node is empty */
                        if (dir->devices[idx] == NULL) {
                                /* Put the file in place */
                                dir->devices[idx] = file;
                                return -E_SUCCESS;
                        }
                        /* else continue */
                }

                last = dir;
                dir = dir->next;
        }

        /* Didn't find an empty spot, and reached the end of the tables
         * We'll just allocate a new table */
        last->next = kmalloc(sizeof(*dir));
        if (last->next == NULL) {
                return -E_NOMEM;
        }
        memset(last->next, 0, sizeof(*dir));
        /* And hook it into the list */
        dir = last->next;

        /* Put the file */
        dir->devices[0] = file;

        /* And return */
        return -E_SUCCESS;
}

static struct devfs_device_node* devfs_get_dir_entry(
                struct devfs_device_node* dir, struct path_directory_node* node)
{
        /* Verify arguments */
        if (dir == NULL || node == NULL) {
                return NULL ;
        }

        /* Make sure we're looking through a directory */
        if (dir->node_type != DEVFS_NODE_DIRECTORY) {
                return NULL ;
        }

        /* Determine the file name length */
        size_t node_len = strlen(node->name);
        if (node_len >= FS_MAX_NAMELEN) {
                return NULL ;
        }

        /* get the directory structure (Allocate if non-existant) */
        struct devfs_device_directory* directory = dir->directory;
        if (directory == NULL) {
                directory = kmalloc(sizeof(*directory));
                if (directory == NULL) {
                        return NULL ;
                }
                memset(directory, 0, sizeof(*directory));
                dir->directory = directory;
        }

        /* Loop through the list of directories
         * (Can be done quicker if tree is used, requires string compare enabled
         * tree though)
         */
        while (directory != NULL ) {
                /* While not at the end of the directory node */
                int idx = 0;
                for (; idx < DEVFS_DIRNODE_SIZE; idx++) {
                        /* If file name lenthts don't match, continue */
                        char* ent_name = directory->devices[idx]->name;
                        if (strlen(ent_name) != node_len) {
                                continue;
                        }

                        /* Names do match, is this our victim? */
                        int cmp = memcmp(node->name, ent_name, node_len);
                        if (cmp == 0) {
                                /* Looks like it is, return our victim */
                                return directory->devices[idx];
                        }
                }
                /* Haven't found the victim yet, keep on looking in the
                 * next table*/
                directory = directory->next;
        }

        return NULL ;
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
        for (; p != NULL ; p = p->next) {
                if (dev == NULL) {
                        success = -E_CORRUPT;
                        break;
                }
                if (p->next == NULL) {
                        /* Add device here */
                        if (devfs_get_dir_entry(dev, p) != NULL) {
                                /* But not if it already exists */
                                success = -E_ALREADY_INITIALISED;
                                break;
                        }
                        if (devfs_put_dir_entry(dev, d) != -E_SUCCESS) {
                                success = -E_GENERIC;
                                break;
                        } else {
                                success = -E_SUCCESS;
                                break;
                        }

                } else if (dev->node_type == DEVFS_NODE_DIRECTORY) {
                        /*
                         * Node is directory, use this to move on to the next
                         * node, until we can add the device file
                         */
                        struct devfs_device_node* tmp;
                        tmp = devfs_get_dir_entry(dev, p);
                        if (tmp == NULL) {
                                tmp = kmalloc(sizeof(*tmp));
                                if (tmp == NULL) {
                                        success = -E_NOMEM;
                                        break;
                                }
                                memset(tmp, 0, sizeof(*tmp));
                                memcpy(tmp->name, p->name, FS_MAX_NAMELEN);
                                tmp->node_type = DEVFS_NODE_DIRECTORY;
                                tmp->directory = kmalloc(
                                                sizeof(*tmp->directory));
                                if (tmp->directory == NULL) {
                                        success = -E_NOMEM;
                                        kfree_s(tmp, sizeof(*tmp));
                                        break;
                                }
                                memset(tmp->directory, 0,
                                                sizeof(*tmp->directory));
                                if (devfs_put_dir_entry(dev, tmp) != -E_SUCCESS) {
                                        success = -E_GENERIC;
                                        break;
                                }
                        }
                        dev = tmp;
                } else {
                        /* Node found is an actual device ...
                         * something went wrong */
                        success = -E_INVALID_ARG;
                        break;
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

        devfs_root.directory = kmalloc(sizeof(*devfs_root.directory));
        if (devfs_root.directory == NULL) {
                panic("Could not initialise the device filesystem");
        }
        memset(devfs_root.directory, 0, sizeof(*devfs_root.directory));

        return -E_NOFUNCTION;
}
