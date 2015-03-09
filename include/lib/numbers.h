/*
 *  Andromeda
 *  Copyright (C) 2014  Bart Kuivenhoven
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

#ifndef __LIB_NUMBERS_H
#define __LIB_NUMBERS_H

#include <types.h>
#include <thread.h>

#define LIB_NUM_ALLOC_FREE 0
#define LIB_NUM_ALLOC_USED 1

struct lib_numset {
        /* start_num is base number divided by alloc_width */
        int32_t start_num;
        /* end_num is the final number divided by alloc_width */
        int32_t end_num;
        /* alloc_width decides the distance between the allocated numbers */
        int32_t alloc_width;

        /* alloc_idx determines where to get the next free number */
        idx_t alloc_idx;

        /* the table containing the allocation data */
        int32_t* alloc_table;
        size_t table_size;

        /* The lock to make sure the allocation has been made atomically */
        mutex_t lock;

        int32_t (*get_next)(struct lib_numset* table);
        int32_t (*free)(struct lib_numset* table, int32_t number);
        int32_t (*check_free)(struct lib_numset* table, int32_t number);
        int32_t (*destroy)(struct lib_numset* table);

};

struct lib_numset* num_alloc_init(int32_t base, int32_t end, int32_t width);

#ifdef NUM_ALLOC_TEST
int num_alloc_test();
#endif

#endif
