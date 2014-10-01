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

#include <lib/numbers.h>
#include <andromeda/system.h>
#include <thread.h>

#ifdef SLAB
#include <mm/cache.h>

struct mm_cache* numset_cache = NULL;
mutex_t numset_lock = mutex_unlocked;
#endif

static int32_t num_alloc_get_next(struct lib_numset* table)
{
        if (table == NULL)
                return -E_NULL_PTR;
        return 0;
}

static int32_t num_alloc_free(struct lib_numset* table, int32_t number)
{
        if (table == NULL)
                return -E_NULL_PTR;

        if (number/table->alloc_width < table->start_num)
                return -E_CORRUPT;
        if (number/table->alloc_width >= table->end_num)
                return -E_CORRUPT;

        return 0;
}

static int32_t num_alloc_destroy(struct lib_numset* table)
{
        if (table == NULL)
                return -E_NULL_PTR;
        return 0;
}

struct lib_numset* num_alloc_init(int32_t base, int32_t end, int32_t width)
{
        if (base < 0 || end < base || width <= 0)
                return NULL;
#ifdef SLAB
        if (numset_cache == NULL) {
                mutex_lock(&numset_lock);
                if (numset_cache == NULL) {
                        numset_cache = mm_cache_init("Numsets",
                                        sizeof(struct lib_numset), 0,
                                        NULL,
                                        NULL);
                }
                if (numset_cache == NULL) {
                        mutex_unlock(&numset_lock);
                        return NULL;
                }
                mutex_unlock(&numset_lock);
        }
        struct lib_numset* numset = mm_cache_alloc(numset_cache, 0);
#else
        struct lib_numset* numset = kmalloc(sizeof(*numset));
#endif

        if (numset == NULL)
                return NULL ;
        memset(numset, 0, sizeof(*numset));

        /* Set up the allocation parameters*/
        numset->alloc_width = width;
        numset->start_num = base / width;
        numset->end_num = base / width;

        /* Determine the range size */
        size_t size = numset->end_num - numset->start_num ;
        size *= sizeof(*numset->alloc_table);

        /* Allocate a space large enough for that range */
        numset->alloc_table = kmalloc(size);
        if (numset->alloc_table == NULL)
                goto cleanup;
        memset(numset->alloc_table, 0, size);

        /* Install the function pointers */
        numset->get_next = num_alloc_get_next;
        numset->free = num_alloc_free;
        numset->destroy = num_alloc_destroy;

        /* We should now be good to go */
        return numset;

cleanup:
#ifdef SLAB
        mm_cache_free(numset_cache, numset);
#else
        kfree(numset);
#endif
        return NULL;
}
