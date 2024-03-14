/* free_list.c - block free list for COFS
 *  Layout based on https://sites.cs.ucsb.edu/~rich/class/cs270/papers/fs-impl.pdf
 */

// TODO: add locking for concurrency support
// TODO: add error checking in places that don't have it!
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include "cofs_parameters.h"
#include "cofs_util.h"
#include "free_list.h"
#include "cofs_data_structures.h"
#include "layer0.h"
#include "superblock.h"

#define ENTRIES_PER_FREEBLOCK   ((COFS_BLOCK_SIZE / sizeof(block_reference)) - 1)

typedef struct freelist_block {
    block_reference next;
    block_reference data[ENTRIES_PER_FREEBLOCK];
} *list_node;
_Static_assert(sizeof(struct freelist_block) == COFS_BLOCK_SIZE, "");

// just to make typing easier LOL
typedef block_reference blkref;

// number of data blocks in the FS
static size_t block_count; // TODO: make use of this to check error stuff

static blkref list_head_blkidx;
static struct freelist_block list_head;
static const list_node head_ptr = &list_head;
static size_t next_freeslot = 0;
static blkref tail_idx = 0;

static inline size_t intdiv_ceil(size_t dividend, size_t divisor)
{
        // NOTE: the addition *can* overflow if numbers are large enough, but
        // we're not currently using it in any scenario where that could occur
        return (dividend + divisor - 1) / divisor;
}

// populate a brand new block in the freelist at `my_blocknum`
void __create_starting_flist_block(blkref my_blocknum, list_node me, size_t first_entry)
{
        me->next = my_blocknum + (ENTRIES_PER_FREEBLOCK - first_entry) + 1;
        if (me->next >= sblock_incore.n_blocks)
                me->next = 0;

        for (size_t i = first_entry; i < ENTRIES_PER_FREEBLOCK; i++) {
                // add one first so we don't put ourselves at 0
                me->data[i] = ++my_blocknum;
        }
}

bool FreeList_create(size_t n_data_blocks, blkref head)
{
        --n_data_blocks; // subtract one to account for the head being stored in the superblock
        size_t n_freelistblocks = intdiv_ceil(n_data_blocks, ENTRIES_PER_FREEBLOCK + 1);
        size_t leftover = n_data_blocks % (ENTRIES_PER_FREEBLOCK + 1);

        size_t start_idx = ENTRIES_PER_FREEBLOCK - leftover;
        list_node node;
        MALIGN_CHECK(node, sizeof(struct freelist_block));

        sblock_incore.flist_head = head;

        for (size_t i = 0; i < n_freelistblocks; i++) {
                __create_starting_flist_block(head, node, start_idx);
                if (layer0_writeBlock(head, node) == -1)
                        return false;
                head = node->next;
                start_idx = 0;
        }
        node->next = 0;

        free(node);
        return true;
}

bool FreeList_init(block_reference head)
{
        list_head_blkidx = head;
        bool ret = layer0_readBlock(head, head_ptr) == 0;
        next_freeslot = ENTRIES_PER_FREEBLOCK - 1;

        while (list_head.data[next_freeslot] != 0 && next_freeslot != SIZE_MAX)
                --next_freeslot;

        return ret;
}

// gets the new head of the free list after we've used up the old one
void __update_head(void)
{
        blkref new_head = head_ptr->next;
        sblock_incore.flist_head = new_head;
        update_superblock();
        list_head_blkidx = new_head;
        layer0_readBlock(new_head, head_ptr);
}

block_reference FreeList_pop(void)
{
        if (list_head_blkidx == 0)
                return 0; // No more free blocks available

        for (next_freeslot += 1; next_freeslot < ENTRIES_PER_FREEBLOCK; next_freeslot++) {
                blkref cand;
                if ((cand = list_head.data[next_freeslot]) != 0) {
                        // zero out the block on-disk
                        if (layer0_writeBlock(cand, ZERO_BLOCK) == -1)
                                return false;
                        list_head.data[next_freeslot] = 0;
                        layer0_writeBlock(list_head_blkidx, head_ptr);
                        --sblock_incore.free_blocks;
                        return cand;
                }
        }

        next_freeslot = SIZE_MAX;
        blkref ret = list_head_blkidx;
        __update_head();
        // zero out the block on-disk
        if (ret != 0 && layer0_writeBlock(ret, ZERO_BLOCK) == -1)
                return 0;
        --sblock_incore.free_blocks;
        return ret;
}

// update's the list's tail block's `next` pointer to reference the new_tail block
void __update_tail(blkref new_tail)
{
	if (list_head.next == 0)
                list_head.next = new_tail;

	list_node old_tail;
        MALIGN_CHECK(old_tail, sizeof(struct freelist_block));
        if (tail_idx == 0) {
                // find the list's tail if we don't know it already
                old_tail->next = list_head_blkidx;
                do {
                        tail_idx = old_tail->next;
                        assert(layer0_readBlock(tail_idx, old_tail) == 0);
                } while (old_tail->next != 0);
        } else {
                layer0_readBlock(tail_idx, old_tail);
        }

        old_tail->next = new_tail;
        layer0_writeBlock(tail_idx, old_tail);

        tail_idx = new_tail;
        free(old_tail);
}

bool FreeList_append(block_reference block_index)
{
        if (block_index >= NUM_BLOCKS || block_index <= sblock_incore.ilist_size)
                return false;
	
	// handle case for empty list
	if (list_head_blkidx == 0) {
                memcpy(head_ptr, ZERO_BLOCK, COFS_BLOCK_SIZE);
		list_head.next = block_index;
		__update_head();
                ++sblock_incore.free_blocks;
		return true;
	}

        // check if there are any open slots on the current list head's data
        for (; next_freeslot != SIZE_MAX; next_freeslot--) {
                if (list_head.data[next_freeslot] == 0) {
                        list_head.data[next_freeslot--] = block_index;
                        layer0_writeBlock(list_head_blkidx, head_ptr);
                        ++sblock_incore.free_blocks;
                        return true;
                }
        }

        next_freeslot = SIZE_MAX;
        // if not, create a new free list block (using the block we're trying to append)
        // and update the old tail
        list_node new_tail;
        MALIGN_CHECK(new_tail, sizeof(struct freelist_block));
        memset(new_tail, 0, sizeof(struct freelist_block));
        __update_tail(block_index);
        bool ret = layer0_writeBlock(block_index, new_tail) != -1;
        free(new_tail);
        if (ret)
                ++sblock_incore.free_blocks;
        return ret;
}

// comparison function for qsort() that will give us an ascending order
int __blkref_comp(const void *a, const void *b)
{
        blkref arg1 = *(const int *) a;
        blkref arg2 = *(const int *) b;

        return (arg1 > arg2) - (arg1 < arg2);
}

void __fsck_print_extra(blkref smaller[], blkref larger[], size_t smaller_sz, size_t larger_sz)
{
        // this doesn't work lol but i don't need it anymore
        size_t l = 0, s = 0;
        while (l < larger_sz && s < smaller_sz) {
                while (smaller[s] != larger[l]) {
                        PRINT_ERR("%zu, ", larger[l]);
                        ++l;
                }
                ++s; ++l;
        }
        puts("");
}

size_t __fsck_insert_sort(blkref **list, blkref elem, size_t count, size_t n_freeblocks)
{
        size_t j;

        // error condition: grow our copy of the freelist larger than the one we received
        if (count >= n_freeblocks) {
                *list = reallocarray(*list, count+1, sizeof(blkref));
                if (*list == NULL) // realloc failed
                        return -1;
        }

        // shift larger elements right to make space for the new one
        for (j = count; j > 0 && (*list)[j - 1] > elem; j--) {
                (*list)[j] = (*list)[j - 1];
        }

        // insert the new element into the correct position
        (*list)[j] = elem;
        return j;
}

bool FreeList_fsck(block_reference head, blkref free_blocks[], size_t n_freeblocks)
{
        /* check the following criteria:
         * 1. The `free_blocks` array contains the same elements as the freelist
         * 2. The freelist contains no cycles or partitions
         * 3. There are no gaps in the freelist (same as #2)
         *
         * Note that we should function correctly when the FS is unmounted, meaning we can't
         * rely on any static internal state utilized by the rest of the FreeList code.
         */

        // special case: freelist is empty (head is #0 in superblock)
        if (head == 0) {
                if (n_freeblocks == 0) {
                        printf("fsck: FreeList appears intact.\n");
                        return true;
                } else {
                        printf("fsck: found 0 free blocks out of an expected %zu\n",
                               n_freeblocks);
                        return false;
                }
        }

        bool ret = true;

        blkref *my_freelist = calloc(n_freeblocks, sizeof(blkref));
        size_t count = 0; // count of elements in on-disk freelist
        // do an insertion sort into `my_freelist` as we iterate over the on-disk freelist
        // so that we can compare it to the `free_blocks` array
        list_node flist_block = aligned_alloc(sizeof(struct freelist_block), sizeof(struct freelist_block));
        blkref next = head;
        do {
//                PRINT_DBG("fsck: reading block %zu\n", next);
                memset(flist_block, 0, sizeof(struct freelist_block));
                if (layer0_readBlock(next, flist_block) != 0) {
                        ret = false;
                        PRINT_ERR("fsck: unable to read freelist block #%zu\n",
                                  next);
                        goto cleanup;
                }
                size_t i;
                blkref *data = flist_block->data;

                __fsck_insert_sort(&my_freelist, next, count++, n_freeblocks);

                for (i = ENTRIES_PER_FREEBLOCK - 1; i != SIZE_MAX; i--) {
                        blkref datablk = data[i];
                        if (datablk == 0) {
                                // check for holes in free list
                                if (i > 0 && data[i-1] != 0)
                                {
                                        PRINT_ERR("fsck: hole found in freelist block #%zu"
                                                  " at position %zu out of %zu\n", next, i, ENTRIES_PER_FREEBLOCK-1);
                                        ret = false;
                                }
                                break;
                        }

                        // look for appropriate place to insert the elements, growing the array if needed
                        size_t pos = __fsck_insert_sort(&my_freelist, datablk, count++, n_freeblocks);
                        if (pos == SIZE_MAX) {
                                // reallocarray() call failed
                                ret = false;
                                goto cleanup;
                        }

                        // check if we stopped because we encountered a duplicate of ourself
                        if (pos > 0 && my_freelist[pos - 1] == datablk) {
                                ret = false;
                                PRINT_ERR("fsck: block #%zu is present multiple"
                                          " times in the freelist!\n",
                                          datablk);
                        }
                }

                next = flist_block->next;

        } while (next != 0);

        if (count != n_freeblocks)
                ret = false;

        printf("fsck: found %zu free data blocks out of an expected %zu\n", count, n_freeblocks);

        // sort the free_blocks array so we can binary search it
        qsort(free_blocks, n_freeblocks, sizeof(blkref), &__blkref_comp);

        if (!ret) {
                if (count > n_freeblocks) {
                        PRINT_ERR("fsck: on-disk freelist is larger than"
                                  " number of unallocated data blocks!\n");
                        PRINT_ERR("\t%zu extra blocks: \n", count - n_freeblocks);
                        __fsck_print_extra(free_blocks, my_freelist, n_freeblocks, count);
                } else if (count < n_freeblocks) {
                        PRINT_ERR("fsck: on-disk freelist is smaller than"
                                  " number of unallocated data blocks!\n");
                        PRINT_ERR("\tMissing %zu blocks: \n", n_freeblocks - count);
                        __fsck_print_extra(my_freelist, free_blocks, count, n_freeblocks);
                }
                goto cleanup;
        }

        /** OKAY, now we have a sorted, in-memory copy of the on-disk freelist.
         * Next step is to compare it to the `free_block` array that we received
         * from the caller. The case where the two arrays are of different size should
         * have already been handled.
         */

        for (size_t i = 0; i < count; i++) {
                if (free_blocks[i] != my_freelist[i]) {
                        ret = false;
//                        PRINT_ERR("fsck: block %zu not present in on-disk freelist!\n",
//                                  free_blocks[i]);
                }
        }

        if (ret)
                printf("fsck: FreeList appears intact.\n");

cleanup:
        free(flist_block);
        free(my_freelist);
        return ret;
}
