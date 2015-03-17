/*
 Andromeda
 Copyright (C) 2015  Bart Kuivenhoven

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESbuffer_initS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <andromeda/drivers.h>
#include <ioctl.h>
#include <stdio.h>
#include <drivers/tty.h>
#include <andromeda/system.h>
#include <lib/tree.h>

#define MAX_LINE_LENGTH (1000)
#define TAB_WIDTH (8)
#define BACKSPACE (-1)
#define DEFAULT_TTY_DEPTH (800)
#define DEFAULT_TTY_LINE_WIDTH (80)
#define DEFAULT_TTY_LINE_HEIGHT (25)

struct tty_line {
        /* Actual length of this line */
        int16_t length;
        /* Character offset in stream */
        size_t offset;

        /* And the data for the line */
        char line[MAX_LINE_LENGTH];
};

struct tty_data {
        /* How long are the lines */
        uint16_t line_width;
        /* How many lines do we have on screen */
        size_t frame_height;

        /* Where our frame ends, we can work back from there */
        size_t frame_line;

        /* On which line we can find our cursor */
        size_t cursor_line;
        /* On which character we can find the cursor */
        int16_t cursor_idx;

        /* How many lines do we accept maximally in our buffers */
        size_t buffer_depth;
        /* Put a lock on it, to keep it safe */
        mutex_t tty_lock;

        /* Data for stdin */
        struct tree_root* input_buffer;
        /* Data for stdout */
        struct tree_root* output_buffer;

        /* Terminal file */
        struct vfile* terminal;
};

static struct tty_line* drv_tty_get_line(struct tree_root* line_buffer,
                size_t line_idx)
{
        struct tty_line* line = line_buffer->find(line_idx, line_buffer);
        if (line == NULL) {
                line = kmalloc(sizeof(*line));
                if (line == NULL) {
                        return NULL ;
                }
                memset(line, ' ', sizeof(*line));
                line->offset = line_idx;
                line_buffer->add(line_idx, line, line_buffer);
        }
        return line;
}

static struct tty_data* drv_tty_get_data_obj(struct device* dev)
{
        if (dev == NULL || dev->device_data == NULL) {
                return NULL ;
        }

        struct tty_data* data = dev->device_data;
        if (dev->device_data_size != sizeof(*data)) {
                return NULL ;
        }
        return data;
}

static size_t drv_tty_read_from_tty(struct vfile* this, char* buf, size_t idx,
                size_t len)
{
        /* Verify the parameters */
        if (len == 0 || this == NULL || buf == NULL) {
                return 0;
        }

        /* Find the right data object */
        struct tty_data* data = drv_tty_get_data_obj(dev_get_device(this));
        if (data == NULL) {
                return 0;
        }

        /* Prepare some loop parameters */
        size_t copied = 0;

        size_t stream_idx = idx / data->line_width;
        int16_t line_idx = idx % data->line_width;

        /* Lock the data */
        mutex_lock(&data->tty_lock);

        struct tty_line* line = drv_tty_get_line(data->input_buffer,
                        stream_idx);
        /* If the data can't be found, unlock and return */
        if (line == NULL) {
                goto cleanup;
        }

        for (; copied < len; copied++) {
                if (line_idx >= data->line_width) {
                        /* If a new line is needed:
                         *   Reset the line index
                         *   Increment the line counter
                         *   Get the next line
                         */
                        line_idx = 0;
                        stream_idx++;

                        line = drv_tty_get_line(data->input_buffer, stream_idx);
                        if (line == NULL) {
                                /* If unable to continue, unlock and return */
                                goto cleanup;
                        }
                }
                switch (line->line[line_idx]) {
                case '\n':
                        /* If a newline was found, go to next line */
                        buf[copied] = '\n';
                        line_idx = 0;
                        stream_idx++;

                        line = drv_tty_get_line(data->input_buffer, stream_idx);
                        if (line == NULL) {
                                /* If line not found, clean our stuff up */
                                goto cleanup;
                        }

                        break;
                default:
                        /* Copy to buffer */
                        buf[copied] = line->line[line_idx];
                        line_idx++;
                        break;
                }
        }

        cleanup:
        /* Move the cursor and then unlock */
        data->cursor_line = stream_idx;
        data->cursor_idx = line_idx;

        mutex_unlock(&data->tty_lock);

        return 0;
}

static size_t drv_tty_write_to_tty(struct vfile* this, char* buf, size_t idx,
                size_t len)
{
        /* Verify the parameters */
        if (len == 0 || this == NULL || buf == NULL) {
                return 0;
        }

        /* Get te data object */
        struct tty_data* data = drv_tty_get_data_obj(dev_get_device(this));
        if (data == NULL) {
                return 0;
        }

        /* Configure line parameters */
        size_t copied = 0;

        size_t stream_idx = idx / data->line_width;
        int16_t line_idx = idx % data->line_width;

        /* Enter critical section */
        mutex_lock(&data->tty_lock);

        /* Get the line to write to */
        struct tty_line* line = drv_tty_get_line(data->output_buffer,
                        stream_idx);
        /* If line not found, leave critical and return */
        if (line == NULL) {
                goto cleanup;
        }

        /* Do the copying */
        for (; copied < len; copied++) {
                /* If at end of line */
                if (line_idx >= data->line_width) {
                        /* Reset line cursor */
                        line_idx = 0;
                        /* Increment line index */
                        stream_idx++;

                        /* Get new line */
                        line = drv_tty_get_line(data->output_buffer,
                                        stream_idx);
                        /* If line not found, cleanup and return */
                        if (line == NULL) {
                                goto cleanup_written;
                        }
                }

                /* Switch on character */
                switch (buf[copied]) {
                case '\n':
                        /* New line found, reset line */
                        line->line[line_idx] = '\n';
                        line_idx = 0;
                        stream_idx++;

                        /* Get next line */
                        line = drv_tty_get_line(data->output_buffer,
                                        stream_idx);

                        /* Cleanup if line not found! */
                        if (line == NULL) {
                                goto cleanup_written;
                        }
                        break;
                case '\r':
                        /* Carriage return, reset line index */
                        line_idx = 0;
                        break;
                case '\t':
                        /*
                         * Tab character found, skip until line_idx % TABWIDTH
                         * equals 0
                         */
                        line_idx += TAB_WIDTH - (line_idx % TAB_WIDTH);
                        break;
                case '\b':
                        /* Backspace found, move back */
                        line_idx += BACKSPACE;
                        break;
                default:
                        /* No command character found, just copy the character */
                        line->line[line_idx] = buf[copied];
                        line_idx++;
                        break;
                }
        }

        cleanup_written:
        /* Update cursor on screen */
        data->cursor_line = stream_idx;
        data->cursor_idx = line_idx;

        cleanup:
        /* Unlock */
        mutex_unlock(&data->tty_lock);
        /* Return */
        return copied;
}

static int dev_tty_ioctl(struct vfile* this, ioctl_t request, void* data)
{
        if (this == NULL || data == NULL) {
                return -E_NULL_PTR;
        }

        struct device* dev = device_find_id(this->fs_data.device_id);
        if (dev == NULL) {
                return -E_INVALID_ARG;
        }

        switch (request) {
        case IOCTL_TTY_RESIZE:
                break;
        case IOCTL_TTY_GET_SIZE:
                break;
        case IOCTL_TTY_SET_BUFLEN:
                break;
        case IOCTL_TTY_GET_BUFLEN:
                break;
        case IOCTL_TTY_SET_TERMINAL:
                break;
        case IOCTL_TTY_GET_TERMINAL:
                break;
        default:
                return -E_INVALID_ARG;
                break;
        }

        return -E_SUCCESS;
}

static struct vfile* drv_tty_open(struct device* dev)
{
        if (dev == NULL || dev->driver == NULL || dev->driver->io == NULL) {
                return NULL ;
        }

        dev->driver->io->open(dev->driver->io, NULL, 0);

        return dev->driver->io;
}

static void drv_tty_stop(struct device* device)
{
        /* Still needs hooks and implementing */
        return;
}

struct device* drv_tty_start(struct device* parent, int num)
{
        if (parent == NULL) {
                return NULL ;
        }

        struct device* tty = kmalloc(sizeof(*tty));
        if (tty == NULL) {
                return NULL ;
        }

        struct tty_data* tty_data = kmalloc(sizeof(*tty_data));
        if (tty_data == NULL) {
                kfree_s(tty, sizeof(*tty));
                return NULL ;
        }

        memset(tty, 0, sizeof(*tty));
        memset(tty_data, 0, sizeof(*tty_data));
        tty_data->input_buffer = tree_new_avl();
        if (tty_data->input_buffer == NULL) {
                // Do cleanup
                kfree_s(tty_data, sizeof(*tty_data));
                kfree_s(tty, sizeof(*tty));
                return NULL ;
        }

        tty_data->output_buffer = tree_new_avl();
        if (tty_data->output_buffer == NULL) {
                // Do cleanup
                tty_data->input_buffer->purge(tty_data->input_buffer, NULL,
                                NULL);
                kfree_s(tty_data, sizeof(*tty_data));
                kfree_s(tty, sizeof(*tty));
                return NULL;
        }

        tty_data->buffer_depth = DEFAULT_TTY_DEPTH;
        tty_data->line_width = DEFAULT_TTY_LINE_WIDTH;
        tty_data->frame_height = DEFAULT_TTY_LINE_HEIGHT;
        tty_data->tty_lock = mutex_unlocked;

        sprintf(tty->name, "tty%i", num);
        tty->type = TTY;

        /* Adds a device file to the driver structure */
        dev_setup_driver(tty, drv_tty_read_from_tty, drv_tty_write_to_tty,
                        dev_tty_ioctl);
        tty->open = drv_tty_open;
        /* Prevent the device file from being cleaned up when not in use */
        tty->open(tty);

        return tty;
}
