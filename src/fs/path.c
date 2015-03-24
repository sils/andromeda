/*
 *  Andromeda
 *  Copyright (C) 2011 - 2015 Bart Kuivenhoven
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

#include <stdlib.h>
#include <fs/path.h>
#include <andromeda/system.h>

void path_clean(struct path_directory_node* elements)
{
        struct path_directory_node *carriage = elements;
        for (; carriage != NULL ; carriage = carriage->next) {
                kfree(carriage);
        }
}

static void add_character(struct path_directory_node* element, char c)
{
        if (element->cursor == 0xff)
                return;
        element->name[element->cursor] = c;
        element->cursor++;
}

struct path_directory_node* path_parse(char* path)
{
        if (path == NULL) {
                return NULL ;
        } else if (strlen(path) == 0) {
                return NULL ;
        }

        struct path_directory_node *list = kmalloc(sizeof(*list));
        memset(list, 0, sizeof(*list));

        struct path_directory_node *carriage = list;
        int idx = 0;
        int name_len = 0;
        boolean escaped = FALSE;

        for (; path[idx] != '\0'; idx++, name_len++) {
                switch (path[idx]) {
                case '\\':
                        if (escaped) {
                                add_character(carriage, '\\');
                                escaped = FALSE;
                        } else {
                                escaped = TRUE;
                        }
                        break;
                case '/':
                        add_character(carriage, '/');
                        if (!escaped) {
                                carriage->next = kmalloc(sizeof(*carriage));
                                if (carriage->next == NULL) {
                                        path_clean(list);
                                        return NULL ;
                                }
                                memset(carriage->next, 0, sizeof(*carriage));
                                carriage = carriage->next;
                                name_len = 0;
                        }
                        escaped = FALSE;
                        break;
                default:
                        add_character(carriage, path[idx]);
                        escaped = FALSE;
                }
                if (name_len > FS_MAX_NAMELEN) {
                        path_clean(list);
                        return NULL ;
                }
        }

        return list;
}
