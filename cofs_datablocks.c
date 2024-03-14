//
// Created by ryan on 11/28/23.
//

#include "cofs_datablocks.h"

#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>

#include "layer0.h"
#include "cofs_data_structures.h"
#include "free_list.h"
#include "cofs_inode_functions.h"
#include "cofs_errno.h"

// iterate over all levels of data blocks in an inode and call func() on each one
// you could definitely do this recursively in much less repetitive code but I can't be bothered
bool
foreach_datablock_in_inode(cofs_inode *inode, datablock_foreach func, size_t start_block, bool stop_on_false,
                           void *other)
{
        // treat all inodes as files, since both dirs and files have the same direct/indirect
        // block layout

        size_t curr_block = 0;
        /* direct blocks */
        bool foreach_direct_block(block_reference *dir_blocks, size_t len)
        {
                bool ret = true;
                for (size_t b = 0; b < len; b++) {
                        block_reference block = dir_blocks[b];
                        if (block == 0)
                                return ret;

                        if (curr_block++ >= start_block)
                                ret = (*func) (block, other) && ret;

                        if (stop_on_false && !ret)
                                return false;
                }

                return ret;
        }

        /* single indirect blocks */
        bool foreach_1indirect_block(block_reference *indir_blocks, size_t len)
        {
                // LMAO for some reason calloc() segfaults *inside* glibc here? not so when using mmap()?
//                block_reference *blockbuf = calloc(N_DIRECT_BLOCKS, sizeof(block_reference));
                block_reference *blockbuf = mmap(NULL, N_DIRECT_BLOCKS * sizeof(block_reference), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
                if (blockbuf == MAP_FAILED)
                        COFS_ERROR(ENOMEM);

                bool ret = true;
                for (size_t b = 0; b < len; b++) {
                        block_reference block = indir_blocks[b];
                        if (block == 0)
                                goto finish_1indir;

                        layer0_readBlock(block, blockbuf);
                        ret = foreach_direct_block(blockbuf, BLOCKS_PER_INDIRECT)
                                && ret;

                        if (stop_on_false && !ret) {
                                ret = false;
                                goto finish_1indir;
                        }
                }

        finish_1indir:
//                free(blockbuf);
                munmap(blockbuf, N_DIRECT_BLOCKS * sizeof(block_reference));
                return ret;
        }

        /* double indirect blocks */
        bool foreach_2indirect_block(block_reference *indir2_blocks, size_t len)
        {
//                block_reference *blockbuf = calloc(N_1INDIRECT_BLOCKS, sizeof(block_reference));
                block_reference *blockbuf = mmap(NULL, N_1INDIRECT_BLOCKS * sizeof(block_reference), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
                if (blockbuf == MAP_FAILED)
                        COFS_ERROR(ENOMEM);

                bool ret = true;
                for (size_t b = 0; b < len; b++) {
                        block_reference block = indir2_blocks[b];
                        if (block == 0)
                                goto finish_2indir;

                        layer0_readBlock(block, blockbuf);
                        ret = foreach_1indirect_block(blockbuf, BLOCKS_PER_INDIRECT)
                                && ret;

                        if (stop_on_false && !ret) {
                                ret = false;
                                goto finish_2indir;
                        }
                }

        finish_2indir:
//                free(blockbuf);
                munmap(blockbuf, N_1INDIRECT_BLOCKS * sizeof(block_reference));
                return ret;
        }

         /* triple indirect blocks */
        bool foreach_3indirect_block(block_reference *indir3_blocks, size_t len)
        {
//                block_reference *blockbuf = calloc(N_2INDIRECT_BLOCKS, sizeof(block_reference));
                block_reference *blockbuf = mmap(NULL, N_2INDIRECT_BLOCKS * sizeof(block_reference), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
                if (blockbuf == MAP_FAILED)
                        COFS_ERROR(ENOMEM);

                bool ret = true;
                for (size_t b = 0; b < len; b++) {
                        block_reference block = indir3_blocks[b];
                        if (block == 0)
                                goto finish_3indir;

                        layer0_readBlock(block, blockbuf);
                        ret = foreach_2indirect_block(blockbuf, BLOCKS_PER_INDIRECT)
                                && ret;

                        if (stop_on_false && !ret) {
                                ret = false;
                                goto finish_3indir;
                        }
                }

        finish_3indir:
//                free(blockbuf);
                munmap(blockbuf, N_2INDIRECT_BLOCKS * sizeof(block_reference));
                return ret;
        }

        if (inode->type != INODE_TYPE_SYML) {
                cofs_file_inode *datablocks = &inode->file;
                return (foreach_direct_block(datablocks->direct_blocks, N_DIRECT_BLOCKS) || !stop_on_false)
                       && (foreach_1indirect_block(datablocks->single_indirect_blocks, N_1INDIRECT_BLOCKS)
                           || !stop_on_false)
                       && (foreach_2indirect_block(datablocks->double_indirect_blocks, N_2INDIRECT_BLOCKS)
                           || !stop_on_false)
                       && (foreach_3indirect_block(datablocks->triple_indirect_blocks, N_3INDIRECT_BLOCKS)
                           || !stop_on_false);
        } else {
                return foreach_direct_block(inode->syml.direct, N_DIRECT_BLOCKS);
        }
}

static block_reference direct_alloc_block_val = 0;
/* Again like the foreach, these 4 can definitely be combined into one function,
 * but I don't want to risk getting it wrong
 */
static bool __alloc_direct(block_reference *blocks, size_t size)
{
        block_reference block = FreeList_pop();
        if (block == 0)
                return false;

        for (size_t i = 0; i < size; i++) {
                if (blocks[i] == 0) {
                        blocks[i] = block;
                        direct_alloc_block_val = block;
                        return true;
                }
        }

        // de-allocate the unused direct block
        FreeList_append(block);
        return false;
}

static bool __alloc_1indirect(block_reference *blocks, size_t size, size_t my_blocks)
{
        size_t first_unused;
        bool ret = false;
        for (first_unused = 0; first_unused < size; first_unused++) {
                if (blocks[first_unused] == 0)
                        break;
        }

        block_reference target_indirect;

        block_reference *indblock = calloc(BLOCKS_PER_INDIRECT, sizeof(block_reference));
        if (indblock == NULL)
                return false;

        bool cleanup_newblock = false;

        // determine if we need to allocate a whole new indirect block or just another direct inside of our indirect
        if (my_blocks % BLOCKS_PER_INDIRECT == 0) {
                // allocate new indirect block
                target_indirect = FreeList_pop();
                if (target_indirect == 0)
                        goto cleanup;

                blocks[first_unused] = target_indirect;
                cleanup_newblock = true;
        } else {
                // allocate new direct inside the partially filled indirect
                assert(first_unused != 0);
                target_indirect = blocks[first_unused - 1];
                if (layer0_readBlock(target_indirect, indblock) == -1)
                        goto cleanup;
        }

        ret = __alloc_direct(indblock, BLOCKS_PER_INDIRECT)
                && (layer0_writeBlock(target_indirect, indblock) == 0);

cleanup:
        free(indblock);

        // de-allocate the empty indirect block if we failed in the lower level
        if (!ret && cleanup_newblock) {
                blocks[first_unused] = 0;
                FreeList_append(target_indirect);
        }

        return ret;
}

static bool __alloc_2indirect(block_reference *blocks, size_t size, size_t my_blocks)
{
        size_t first_unused;
        bool ret = false;
        for (first_unused = 0; first_unused < size; first_unused++) {
                if (blocks[first_unused] == 0)
                        break;
        }

        block_reference target_indirect;
        size_t spare_blocks = 0;

        block_reference *indblock = calloc(BLOCKS_PER_INDIRECT, sizeof(block_reference));
        if (indblock == NULL)
                return false;

        bool cleanup_newblock = false;

        // determine if we need to allocate a whole new indirect block or just another direct inside of our indirect
        if (my_blocks % (BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT) == 0) {
                // allocate new indirect block
                target_indirect = FreeList_pop();
                if (target_indirect == 0)
                        goto cleanup;

                blocks[first_unused] = target_indirect;
                cleanup_newblock = true;
        } else {
                // allocate new direct inside the partially filled indirect
                assert(first_unused != 0);
                target_indirect = blocks[first_unused - 1];
                if (layer0_readBlock(target_indirect, indblock) == -1)
                        goto cleanup;

                // number of direct blocks belonging only to the final partially full double-indirect block
                spare_blocks = my_blocks % (first_unused * BLOCKS_PER_INDIRECT);
        }

        ret = __alloc_1indirect(indblock, BLOCKS_PER_INDIRECT, spare_blocks)
                && (layer0_writeBlock(target_indirect, indblock) == 0);

cleanup:
        free(indblock);

        // de-allocate the empty indirect block if we failed in the lower levels
        if (!ret && cleanup_newblock) {
                blocks[first_unused] = 0;
                FreeList_append(target_indirect);
        }

        return ret;
}

static bool __alloc_3indirect(block_reference *blocks, size_t size, size_t my_blocks)
{
        size_t first_unused;
        bool ret = false;
        for (first_unused = 0; first_unused < size; first_unused++) {
                if (blocks[first_unused] == 0)
                        break;
        }

        block_reference target_indirect;
        size_t spare_blocks = 0;

        block_reference *indblock = calloc(BLOCKS_PER_INDIRECT, sizeof(block_reference));
        if (indblock == NULL)
                return false;

        bool cleanup_newblock = false;

        // determine if we need to allocate a whole new indirect block or just another direct inside of our indirect
        if (my_blocks % (BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT) == 0) {
                // allocate new indirect block
                target_indirect = FreeList_pop();
                if (target_indirect == 0)
                        goto cleanup;

                blocks[first_unused] = target_indirect;
                cleanup_newblock = true;
        } else {
                // allocate new direct inside the partially filled indirect
                assert(first_unused != 0);
                target_indirect = blocks[first_unused - 1];
                if (layer0_readBlock(target_indirect, indblock) == -1)
                        goto cleanup;

                // number of direct blocks belonging only to the final partially full double-indirect block
                spare_blocks = my_blocks % (first_unused * (BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT));
        }

        ret = __alloc_2indirect(indblock, BLOCKS_PER_INDIRECT, spare_blocks)
                && (layer0_writeBlock(target_indirect, indblock) == 0);

cleanup:
        free(indblock);

        // de-allocate the empty indirect block if we failed in the lower levels
        if (!ret && cleanup_newblock) {
                blocks[first_unused] = 0;
                FreeList_append(target_indirect);
        }

        return ret;
}

block_reference alloc_new_datablock(cofs_inode *inode)
{
        static const size_t BLOCKS_IN_1INDIRECT =
                N_1INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT;
        static const size_t BLOCKS_IN_2INDIRECT =
                N_2INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT;
        static const size_t BLOCKS_IN_3INDIRECT =
                N_3INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT;

        // figure out if we need a 0, 1, 2, or 3 indirect block
        size_t nblocks = inode->n_blocks;
        size_t lim = N_DIRECT_BLOCKS;
        bool ret = false;
        if (nblocks < lim)
                ret = __alloc_direct(inode->file.direct_blocks, N_DIRECT_BLOCKS);
        else if (nblocks < (lim += BLOCKS_IN_1INDIRECT))
                ret = __alloc_1indirect(inode->file.single_indirect_blocks,
                                        N_1INDIRECT_BLOCKS, nblocks - N_DIRECT_BLOCKS);
        else if (nblocks < (lim += BLOCKS_IN_2INDIRECT))
                ret = __alloc_2indirect(inode->file.double_indirect_blocks, N_2INDIRECT_BLOCKS,
                                        nblocks - N_DIRECT_BLOCKS - BLOCKS_IN_1INDIRECT);
        else if (nblocks < (lim += BLOCKS_IN_3INDIRECT))
                ret = __alloc_3indirect(inode->file.triple_indirect_blocks, N_3INDIRECT_BLOCKS,
                                        nblocks - N_DIRECT_BLOCKS - BLOCKS_IN_1INDIRECT - BLOCKS_IN_2INDIRECT);

        if (ret) {
                inode->n_blocks++;
        }

        // note: could defer the write_inode() to the caller? not sure if there's any benefit either way
        if (ret && write_inode(inode, inode->inum))
                return direct_alloc_block_val;

        return 0;
}

static bool __getLastDatablock_Iterator(block_reference block, void *block_outptr)
{
        *(block_reference *)block_outptr = block;
        return true;
}

// finds the block number of the last datablock belonging to an inode
block_reference get_last_datablock(cofs_inode *inode)
{
        block_reference result = 0;

        if (!foreach_datablock_in_inode(inode, &__getLastDatablock_Iterator, 0, false, &result))
                return 0;

        return result;
}

static bool release_datablocks_indr(block_reference *blocks, int depth, size_t start, size_t *pos) {
        size_t idx;
        block_reference cur_block;
        int next_depth = depth - 1;
        block_reference *blockbuf = calloc(BLOCKS_PER_INDIRECT, sizeof(block_reference));
        // The current indirect block should only be released if the first address in it is released
        bool first_released = (*pos >= start);
        for (idx = 0; idx < BLOCKS_PER_INDIRECT; ++idx) {
                cur_block = blocks[idx];
                if (cur_block == 0)
                        break; // we can break here because doesn't support holes in files
                if (next_depth > 0) {
                        // Looking at indirect block addresses
                        layer0_readBlock(cur_block, blockbuf);
                        if (release_datablocks_indr(blockbuf, next_depth, start, pos)) {
                                FreeList_append(cur_block);
                        }
                } else  {
                        if (*pos >= start) {
                                FreeList_append(cur_block);
                        }
                        ++(*pos);
                }
        }
        free(blockbuf);
        return first_released;
}

bool release_datablocks(cofs_inode *inode, size_t start)
{
        size_t pos = 0;
        size_t idx;
        block_reference cur_block;
        // Clear all direct blocks
        for (idx = 0; idx < N_DIRECT_BLOCKS; ++idx) {
                cur_block = inode->file.direct_blocks[idx];
                if (cur_block == 0)
                        break;
                if (pos++ >= start) {
                        FreeList_append(cur_block);
                        inode->file.direct_blocks[idx] = 0;
                }
        }
        // Allocate indirect block buffer once, shared among all recursive calls
        block_reference *blockbuf = calloc(BLOCKS_PER_INDIRECT, sizeof(block_reference));
        // Clear single indirect blocks following pos
        for (idx = 0; idx < N_1INDIRECT_BLOCKS; ++idx) {
                cur_block = inode->file.single_indirect_blocks[idx];

                if (cur_block == 0)
                        break;

                layer0_readBlock(cur_block, blockbuf);
                // T only when all addrs at cur_block have been released
                if (release_datablocks_indr(blockbuf, 1, start, &pos)) {
                        FreeList_append(cur_block);
                        inode->file.single_indirect_blocks[idx] = 0;
                }
        }
        // Only 1 double indirect block per inode
        cur_block = inode->file.double_indirect_blocks[0];
        if (cur_block != 0) {
                layer0_readBlock(cur_block, blockbuf);
                if (release_datablocks_indr(blockbuf, 2, start, &pos)) {
                        FreeList_append(cur_block);
                        inode->file.double_indirect_blocks[0] = 0;
                }
                // Similarly, only 1 triple indirect block per inode
                cur_block = inode->file.triple_indirect_blocks[0];
                if (cur_block != 0) {
                        layer0_readBlock(cur_block, blockbuf);
                        if (release_datablocks_indr(blockbuf, 3, start, &pos)) {
                                FreeList_append(cur_block);
                                inode->file.triple_indirect_blocks[0] = 0;
                        }
                }
        }
        free(blockbuf);
        // n_blocks := n_blocks - (n_blocks - start)
        inode->n_blocks = start;
        return true;
}
