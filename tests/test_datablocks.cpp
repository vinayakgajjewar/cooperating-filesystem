//
// Created by ryan on 11/29/23.
//
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

extern "C" {
#define _Static_assert(...)
#include "layer0.h"
#include "free_list.h"
#include "cofs_datablocks.h"
#include "cofs_data_structures.h"
#include "cofs_mkfs.h"
#include "superblock.h"
#include "cofs_parameters.h"
#include "cofs_inode_functions.h"
#undef _Static_assert
};

#define MEGABYTE        (1024UL * 1024)
#define GIGABYTE        (1024UL * MEGABYTE)

/* 8gigs should give us a triple indirect block, but
 * 30megs is much faster, and eucalyptus can't handle
 * 8GB anyway.
 */
static constexpr size_t MEMSIZE = 8 * GIGABYTE;
//static constexpr size_t MEMSIZE = 30 * MEGABYTE;

static bool assert_status = true;
#define ASSERT_EQ(expected, actual)                                      \
        do {                                                                     \
                if ((expected) != (actual)) {                                    \
                        cerr << "Assertion failed at " __FILE__ "#" << __LINE__ << ":\n"        \
                                        "\t '" #expected " == " #actual "'" << endl <<          \
                                        "\texpected: " << expected << endl <<                   \
                                        "\tactual: " << actual << endl;                         \
                        assert_status = false;                                                  \
                }                                                                               \
        } while (0)

using namespace std;

vector<block_reference> intercept_FreeList;
size_t n_datablocks;
block_reference dblock_start;

/*
 * The linker is configured to replace any calls to FreeList_*
 * with the ones below, making it easier for us to track the
 * status of the free list while testing inode block
 * allocation/deallocation
 */
extern "C"
__attribute__((used)) void
__wrap_FreeList_create(size_t n_data_blocks, block_reference head)
{
//        cout << "my freelist create" << endl;
        n_datablocks = n_data_blocks;
        dblock_start = head;

        intercept_FreeList.clear();
        intercept_FreeList.reserve(n_data_blocks);
        intercept_FreeList.resize(n_data_blocks);

        // reverse the order, so smallest elements are at the end of
        // the vector (i.e. the head of our FreeList)
        iota(intercept_FreeList.rbegin(), intercept_FreeList.rend(), head);

        sblock_incore.flist_head = head;
        update_superblock();
}

extern "C"
__attribute__((used)) bool
__wrap_FreeList_init(block_reference head)
{
//        cout << "my freelist init" << endl;
        // do nothing
        return true;
}

extern "C"
__attribute__((used)) block_reference
__wrap_FreeList_pop(void)
{
//        cout << "my freelist pop" << endl;
        auto ret = intercept_FreeList.back();
        intercept_FreeList.pop_back();
        return ret;
}

extern "C"
__attribute__((used)) bool
__wrap_FreeList_append(block_reference blk)
{
//        cout << "my freelist append" << endl;
        intercept_FreeList.push_back(blk);
        __real_FreeList_append(blk);
        return true;
}

static constexpr size_t BLOCKS_IN_1INDIRECT =
        N_1INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT;
static constexpr size_t BLOCKS_IN_2INDIRECT =
        N_2INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT;
static constexpr size_t BLOCKS_IN_3INDIRECT =
        N_3INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT;

void test_blockAllocate(void)
{
        if (!layer0_init(nullptr, MEMSIZE)) {
                cerr << "layer 0 init failed." << endl;
                return;
        }

        cofs_inode ino;
        read_inode(&ino, sblock_incore.root_dir);
        size_t inosize = ino.n_blocks;
        while (!intercept_FreeList.empty()) {
                alloc_new_datablock(&ino);
                ++inosize;
        }
        ASSERT_EQ(inosize, ino.n_blocks);

//        ASSERT_EQ(true, ino.dir.triple_indirect_blocks[0] != 0);

        cout << "[allocated all data blocks to the root directory inode]\n"
                "\tput a breakpoint here in the debugger to examine & verify inode state" << endl;

        /* now, verify that if we deallocate all of the data blocks belonging to our inode, they
         * all end up back in our list
         */
        cout << "verifying that we get all blocks back into our freelist when freeing" << endl;
        release_datablocks(&ino, 0);

        ASSERT_EQ(n_datablocks, intercept_FreeList.size());
        ASSERT_EQ(0, ino.n_blocks);

        sort(intercept_FreeList.begin(), intercept_FreeList.end());

        size_t i;
        for (i = 0; i < n_datablocks; i++) {
                if (i + dblock_start != intercept_FreeList[i]) {
                        fprintf(stderr,
                                "inconsistent datablocks\nexpected %lu actual %lu\n",
                                i + dblock_start, intercept_FreeList[i]);
                        fprintf(stderr, "A few entries following inconsistency\n");
                        for (int j = 1; j < 6 && i + j < n_datablocks; ++j) {
                                fprintf(stderr, "expected %lu actual %lu\n",
                                        i + j + dblock_start, intercept_FreeList[i + j]);
                        }
                        break;
                }
        }
        if (i == n_datablocks)
                cout << "release routines are consistent!" << endl;

        layer0_teardown();
}

extern "C" unsigned char *getmembase(void);

static void reset_fs(cofs_inode &ino_to_clear)
{
        // zero out all data blocks in the fs and reset the FreeList and inode
        std::fill(getmembase() + dblock_start * COFS_BLOCK_SIZE,
                  getmembase() + MEMSIZE,
                  0);

        std::fill_n(ino_to_clear.dir.direct_blocks, N_DIRECT_BLOCKS, 0);
        write_inode(&ino_to_clear, ino_to_clear.inum);
        ino_to_clear.dir.double_indirect_blocks[0] = 0;
        ino_to_clear.dir.triple_indirect_blocks[0] = 0;
        ino_to_clear.n_blocks = 0;
        ino_to_clear.n_bytes = 0;

        __wrap_FreeList_create(n_datablocks, dblock_start);
}

template <std::integral I>
static inline constexpr I intdiv_ceil(I dividend, I divisor)
{
        return (dividend + divisor - 1) / divisor;
}

void test_blockReleasePartial(void)
{
        assert_status = true;
        bool failure = false;
        if (!layer0_init(nullptr, MEMSIZE)) {
                cerr << "layer 0 init failed." << endl;
                return;
        }

        cofs_inode ino;
        read_inode(&ino, sblock_incore.root_dir);
        size_t inosize = ino.n_blocks;

        /* allocate exactly the first 12 direct blocks */
        for (inosize = ino.n_blocks;
            ino.n_blocks < N_DIRECT_BLOCKS;
            ++inosize)
        {
                alloc_new_datablock(&ino);
        }

        ASSERT_EQ(N_DIRECT_BLOCKS, ino.n_blocks);
        ASSERT_EQ(inosize, ino.n_blocks);
        size_t fblocks_post_alloc = intercept_FreeList.size();

        /* now, free only the last 6 */
        release_datablocks(&ino, N_DIRECT_BLOCKS / 2);
        ASSERT_EQ(inosize / 2, ino.n_blocks);
        ASSERT_EQ(fblocks_post_alloc + N_DIRECT_BLOCKS / 2, intercept_FreeList.size());
        if (!assert_status) {
                cerr << "release 1/2 direct blocks failed" << endl;
                failure = true;
        }
        assert_status = true;

        /* here's where things get hairy: test allocating and freeing into the
         * indirect block ranges.
         */

        /* first, single-indirect */
        reset_fs(ino);
        size_t tot_direct_alloc = N_DIRECT_BLOCKS + BLOCKS_IN_1INDIRECT;
        size_t tot_data_alloc = tot_direct_alloc + N_1INDIRECT_BLOCKS;
        for (inosize = ino.n_blocks;
             ino.n_blocks < tot_direct_alloc;
            inosize++)
        {
                alloc_new_datablock(&ino);
        }
        ASSERT_EQ(tot_direct_alloc, ino.n_blocks);
        ASSERT_EQ(n_datablocks - tot_data_alloc, intercept_FreeList.size());
        if (!assert_status) {
                cerr << "alloc all indirect blocks failed" << endl;
                failure = true;
        }
        assert_status = true;

        intercept_FreeList.clear();

        // now free (a) some indirect blocks, then (b) some direct and all indirect blocks
        // note: this math only works if the inode holds no double or triple indirect blocks
        size_t blocks_in_ino_indirect =  // how many of the inode's data blocks are held in indirect blocks
                ino.n_blocks - N_DIRECT_BLOCKS;
        size_t to_free =  // how many blocks will be freed from the inode
                blocks_in_ino_indirect / 2;
        size_t ino_n_indirect =  // number of actual indirect blocks in use by the inode
                intdiv_ceil(blocks_in_ino_indirect, BLOCKS_PER_INDIRECT);
        size_t leftover_in_last_indirect =  // how many data slots in the inode's last occupied inidirect block are in use
                BLOCKS_PER_INDIRECT * ino_n_indirect - blocks_in_ino_indirect;
        size_t indirect_freed = // how many of the inode's indirect blocks should be put back onto the freelist
                // note that the `to_free` value here should be only the number of to-free datablocks that belong to the single-indirect blocks
                (to_free + 1 - leftover_in_last_indirect) / BLOCKS_PER_INDIRECT;

        release_datablocks(&ino, ino.n_blocks - to_free);
        ASSERT_EQ(inosize - to_free, ino.n_blocks);
        ASSERT_EQ(to_free + indirect_freed, intercept_FreeList.size());
        if (!assert_status) { // this doesn't work
                cerr << "partial indirect freeing failed" << endl;
                failure = true;
        }
        assert_status = true;

        // free from both single indirect and direct
//        reset_fs(ino);
//        tot_direct_alloc = N_DIRECT_BLOCKS + BLOCKS_IN_1INDIRECT;
//        tot_data_alloc = tot_direct_alloc + N_1INDIRECT_BLOCKS;
//        for (inosize = ino.n_blocks;
//             ino.n_blocks < tot_direct_alloc;
//            inosize++)
//        {
//                alloc_new_datablock(&ino);
//        }
//        ASSERT_EQ(tot_direct_alloc, ino.n_blocks);
//        ASSERT_EQ(n_datablocks - tot_data_alloc, intercept_FreeList.size());
//
//        intercept_FreeList.clear();
//
//        release_datablocks(&ino, 1);
//        ASSERT_EQ(tot_direct_alloc + N_1INDIRECT_BLOCKS - 1, intercept_FreeList.size());
//        ASSERT_EQ(1, ino.n_blocks);
//        if (!assert_status) { // this works
//                cerr << "free all indirect and some direct failed" << endl;
//                failure = true;
//        }
//        assert_status = true;

        auto &stream = failure ? cerr : cout;

        stream << "partial release routine tests "
                << (failure ? "failed" : "passed")
                << "!" << endl;

        layer0_teardown();
}

int main(int argc, char *argv[])
{
        cout << "yay"<< endl;

        cout << "testing block allocation/deallocation routines" << endl;
        test_blockAllocate();

        cout << "testing partial block deallocation routines" << endl;
        test_blockReleasePartial();

        return 0;
}
