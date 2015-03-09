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

#define NO_ALLOC_LAST -1
#define NO_ALLOC_USED -2

static int32_t num_alloc_get_next(struct lib_numset* table)
{
        if (table == NULL)
                return -E_NULL_PTR;

        mutex_lock(&table->lock);

        int32_t allocated = table->alloc_idx;
        if (allocated == NO_ALLOC_LAST || allocated == NO_ALLOC_USED) {
                allocated = -E_OUTOFRESOURCES;
                goto end;
        }
        table->alloc_idx = table->alloc_table[allocated];
        table->alloc_table[allocated] = NO_ALLOC_USED;

end:
        mutex_unlock(&table->lock);

        if (allocated >= 0) {
                return (allocated * table->alloc_width) + table->start_num;
        }
        return allocated;
}

static int32_t num_alloc_free(struct lib_numset* table, int32_t number)
{
        if (table == NULL)
                return -E_NULL_PTR;

        if (number < table->start_num)
                return -E_CORRUPT;
        if (number > table->end_num)
                return -E_CORRUPT;
        if ((number - table->start_num) % table->alloc_width != 0)
                return -E_CORRUPT;

        number -= table->start_num;
        number /= table->alloc_width;

        mutex_lock(&table->lock);

        int32_t found = table->alloc_table[number];
        if (found != NO_ALLOC_LAST && found != NO_ALLOC_USED){
                found = -E_CORRUPT;
                goto end;
        }

        found = table->alloc_idx;
        if (table->alloc_idx == (idx_t)NO_ALLOC_LAST) {
                table->alloc_idx = number;
                table->alloc_table[number] = NO_ALLOC_LAST;
        } else {
                table->alloc_idx = number;
                table->alloc_table[number] = found;
        }
        found = -E_SUCCESS;
end:
        mutex_unlock(&table->lock);

        return found;
}

static int32_t num_alloc_check_free(struct lib_numset* table, int32_t number)
{
        if (table == NULL)
                return -E_NULL_PTR;

        if (number < table->start_num || number > table->end_num)
                return -E_CORRUPT;
        if ((number - table->start_num) % table->alloc_width != 0)
                return -E_CORRUPT;

        number -= table->start_num;
        number /= table->alloc_width;

        int ret = table->alloc_table[number];
        if (ret != NO_ALLOC_USED)
                return LIB_NUM_ALLOC_FREE;

        return LIB_NUM_ALLOC_USED;
}

static int32_t num_alloc_destroy(struct lib_numset* table)
{
        if (table == NULL)
                return -E_NULL_PTR;

        mutex_lock(&table->lock);
        void* alloc_table = table->alloc_table;
        size_t alloc_table_size = table->table_size;

        memset(alloc_table, 0, alloc_table_size);
        memset(table, 0, sizeof(*table));

        kfree_s(alloc_table, alloc_table_size);
#ifdef SLAB
        mm_cache_free(numset_cache, table);
#else
        kfree(table->alloc_table);
#endif

        return 0;
}

#ifdef NUM_ALLOC_TEST
#define NUM_ALLOC_TEST_OFFSET 0
#define NUM_ALLOC_TEST_SIZE 4
#define NUM_ALLOC_TEST_WIDTH 1

int num_alloc_test()
{
        int error = -E_SUCCESS;
        struct lib_numset* alloc_test = num_alloc_init(NUM_ALLOC_TEST_OFFSET,
                        NUM_ALLOC_TEST_OFFSET + NUM_ALLOC_TEST_SIZE,
                        NUM_ALLOC_TEST_WIDTH);

        if (alloc_test == NULL)
                return -E_NULL_PTR;

        int tests[NUM_ALLOC_TEST_SIZE];
        int i = 0;
        for (; i < NUM_ALLOC_TEST_SIZE; i++)
                tests[i] = alloc_test->get_next(alloc_test);

        for (i = 0; i < NUM_ALLOC_TEST_SIZE; i++) {
                int test = alloc_test->check_free(alloc_test, tests[i]);
                if (test != LIB_NUM_ALLOC_USED) {
                        error |= -E_GENERIC;
                        goto cleanup;
                }
        }

cleanup:
        alloc_test->destroy(alloc_test);
        return -E_NOFUNCTION;
}
#endif

struct lib_numset* num_alloc_init(int32_t base, int32_t length, int32_t width)
{
        if (base < 0 || length <= 0 || width <= 0)
                return NULL;
        size_t size = length / width;
        size *= sizeof(int32_t);
        if (size <= 0)
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
                return NULL;

        memset(numset, 0, sizeof(*numset));

        /* Set up the allocation parameters*/
        numset->alloc_width = width;
        numset->start_num = base;
        numset->end_num = base + length * width;

        /* Determine the range size */

        /* Allocate a space large enough for that range */
        numset->alloc_table = kmalloc(size);
        if (numset->alloc_table == NULL)
                goto cleanup;
        memset(numset->alloc_table, 0, size);

        int idx = 0;
        for (; idx < numset->end_num; idx++) {
                numset->alloc_table[idx] = idx + 1;
        }
        numset->alloc_table[idx - 1] = NO_ALLOC_LAST;
        numset->table_size = size;

        /* Install the function pointers */
        numset->get_next = num_alloc_get_next;
        numset->free = num_alloc_free;
        numset->destroy = num_alloc_destroy;
        numset->check_free = num_alloc_check_free;

        /* We should be good to go by now */
        return numset;

cleanup:
#ifdef SLAB
        mm_cache_free(numset_cache, numset);
#else
        kfree(numset);
#endif
        return NULL;
}
