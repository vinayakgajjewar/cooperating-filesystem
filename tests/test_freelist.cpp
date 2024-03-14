//
// Created by ryan on 11/26/23.
//

#include <vector>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <string>

#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <ctime>
#include <cstring>

extern "C" {
#define _Static_assert(...)
#include "layer0.h"
#include "free_list.h"
#include "cofs_data_structures.h"
#include "cofs_parameters.h"
#include "superblock.h"
#include "cofs_mkfs.h"
#include "cofs_errno.h"
#undef _Static_assert
};

#define MEGABYTE        (1024UL * 1024)
#define GIGABYTE        (1024UL * MEGABYTE)

#define ASSERT_EQ(expected, actual, retvar)                                      \
        do {                                                                     \
                if ((expected) != (actual)) {                                    \
                        cerr << "Assertion failed at " __FILE__ ":" << __LINE__ << endl <<           \
                                        "\texpected: " << expected << endl <<                       \
                                        "\tactual: " << actual << endl;                        \
                        retvar = false;                                          \
                }                                                                \
        } while (0)

using namespace std;

static void test_fsck()
{
        size_t disk_size_in_bytes = 3 * MEGABYTE;
        size_t NUM_BLOCKS = disk_size_in_bytes / COFS_BLOCK_SIZE;
        // Set block numbers for superblock, ilist (10% of disk), and free list
        block_reference const superblock_block_number = 0;
        size_t const ilist_size_in_blocks = NUM_BLOCKS / ILIST_SIZE_FRACTION;
        block_reference const start_of_free_list = (ilist_size_in_blocks) + 1;

        // Set number of data blocks to (total number of blocks on disk - (superblock + blocks in list))
        size_t const number_of_data_blocks = NUM_BLOCKS - (1 + ilist_size_in_blocks);
        if (!layer0_init(NULL, disk_size_in_bytes)) {
                printf("failed to init layer 0\n");
                exit(EXIT_FAILURE);
        }

        /* test the freelist creation */
        vector<block_reference> blocks;
        for (size_t i = 0; i < number_of_data_blocks; i++) {
            blocks.push_back(i + start_of_free_list);
        }
        FreeList_fsck(start_of_free_list, blocks.data(), blocks.size());

        layer0_teardown();
}

static void test_pop()
{
	// NOTE: this test will not work for too small size because
	// when we attempt to refill a block, we run out of space in the FS
        size_t disk_size_in_bytes = 300 * MEGABYTE;
        size_t NUM_BLOCKS = disk_size_in_bytes / COFS_BLOCK_SIZE;
        // Set block numbers for superblock, ilist (10% of disk), and free list
        block_reference const superblock_block_number = 0;
        size_t const ilist_size_in_blocks = NUM_BLOCKS / ILIST_SIZE_FRACTION;
        block_reference const start_of_free_list = (ilist_size_in_blocks) + 1;

        // Set number of data blocks to (total number of blocks on disk - (superblock + blocks in list))
        size_t const number_of_data_blocks = NUM_BLOCKS - (1 + ilist_size_in_blocks);
        if (!layer0_init(NULL, disk_size_in_bytes)) {
                printf("failed to init layer 0\n");
                exit(EXIT_FAILURE);
        }

        vector<block_reference> blocks;
        for (size_t i = 0; i < number_of_data_blocks; i++) {
            blocks.push_back(i + start_of_free_list);
        }
        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

        block_reference br;
        FreeList_init(start_of_free_list);
        cout << "testing full head allocate" << endl;
        size_t head_size = number_of_data_blocks % 512 - 1;
        for (size_t i = 0; i < head_size; i++) {
                br = FreeList_pop();
//                cout << "got " << br << endl;
                erase(blocks, br);
                if (br == start_of_free_list)
                        cout << "got list head" << endl;
        }

        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

        cout << "verifying that next allocated block is the old head of freelist: " <<
                (FreeList_pop() == start_of_free_list) << endl;
        erase(blocks, start_of_free_list);

        cout << "testing adding a new block onto the freelist now" << endl;
        FreeList_append(br);
        blocks.push_back(br);
        sort(blocks.begin(), blocks.end());

        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

	cout << "test emptying out one block completely" << endl;
	for (size_t i = 0; i < 512; i++){
		br = FreeList_pop();
		erase(blocks, br);
	}
	sort(blocks.begin(), blocks.end());
	FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());


        cout << "test filling up and allocating a new storage block" << endl;
        srand(time(nullptr));
        // we need to do some nonsense to avoid duplicate elements since the rand gen is with replacement
        unordered_set<block_reference> no_dups;
        no_dups.insert(blocks.cbegin(), blocks.cend());
        do {
                block_reference newblk;
                do {
                        newblk = rand() % number_of_data_blocks + start_of_free_list;
                } while (no_dups.contains(newblk));

                FreeList_append(newblk);
                no_dups.insert(newblk);
                blocks.push_back(newblk);
        } while (blocks.size() % 512 != 0);

        sort(blocks.begin(), blocks.end());
        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

        layer0_teardown();
}

void test_fill(void)
{
	size_t disk_size_in_bytes = 3 * MEGABYTE;
        size_t NUM_BLOCKS = disk_size_in_bytes / COFS_BLOCK_SIZE;
        // Set block numbers for superblock, ilist (10% of disk), and free list
        block_reference const superblock_block_number = 0;
        size_t const ilist_size_in_blocks = NUM_BLOCKS / ILIST_SIZE_FRACTION;
        block_reference const start_of_free_list = (ilist_size_in_blocks) + 1;

        // Set number of data blocks to (total number of blocks on disk - (superblock + blocks in list))
        size_t const number_of_data_blocks = NUM_BLOCKS - (1 + ilist_size_in_blocks);
        if (!layer0_init(NULL, disk_size_in_bytes)) {
                printf("failed to init layer 0\n");
                exit(EXIT_FAILURE);
        }
	
	FreeList_init(start_of_free_list);

	block_reference br;
	cout << "testing attempting to allocate from empty freelist" << endl;
	unordered_set<block_reference> alloced{};
	for (size_t i = 0; i < number_of_data_blocks; i++) {
		br = FreeList_pop();
		//cout << "\tgot " << br << endl;
		if (alloced.contains(br)) {
			cerr << "\tallocated block #" << br 
				<< " mulitple times!" << endl;
		} else {
			alloced.insert(br);
		}
	}
	bool ret = true;
	ASSERT_EQ(0, FreeList_pop(), ret);
	if (ret)
		cout << "\tpop() returned 0 (success)" << endl;
	
	vector<block_reference> blocks;
	FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

	cout << "now testing that we can push data back into the empty free list"
		<< endl;
	FreeList_append(start_of_free_list);

        blocks.push_back(start_of_free_list);
	FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

	ASSERT_EQ(start_of_free_list, FreeList_pop(), ret);
	if (ret)
		cout << "\tsuccess" << endl;

        blocks.clear();
        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());
	layer0_teardown();
}

static constexpr const char * devpath = "/dev/loop0";
extern "C" size_t getsize(void);

void test_callMkfs(void)
{
        cout << "layer0_init() returned "
                << layer0_init(devpath, 0) << endl;
        cout << "running mkfs..." << endl;
        mkfs(getsize());
}

#define MODIFY_DEV

void test_retreiveMkfs(void)
{
        cout << "initializing layer0... returned "
                << layer0_init(devpath, 0) << endl;

        size_t disk_size_in_bytes = getsize();
        size_t NUM_BLOCKS = disk_size_in_bytes / COFS_BLOCK_SIZE;
        // Set block numbers for superblock, ilist (10% of disk), and free list
        block_reference const superblock_block_number = 0;
        size_t const ilist_size_in_blocks = NUM_BLOCKS / ILIST_SIZE_FRACTION;
        block_reference const start_of_free_list = (ilist_size_in_blocks) + 1;

        // Set number of data blocks to (total number of blocks on disk - (superblock + blocks in list))
        size_t const number_of_data_blocks = NUM_BLOCKS - (1 + ilist_size_in_blocks);

        vector<block_reference> blocks;
        for (size_t i = 0; i < number_of_data_blocks; i++) {
            blocks.push_back(i + start_of_free_list);
        }

        cout << "performing fsck before modifying FreeList" << endl;
        FreeList_fsck(start_of_free_list, blocks.data(), blocks.size());

#ifdef MODIFY_DEV
        block_reference br;
        cout << "FreeList_init() returned " << FreeList_init(start_of_free_list) << endl;
        cout << "testing full head allocate" << endl;
        size_t head_size = ((number_of_data_blocks - 1) % 512);
        for (size_t i = 0; i < head_size; i++) {
                br = FreeList_pop();
                erase(blocks, br);
        }

        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

        cout << "verifying that next allocated block is the old head of freelist: " <<
                (FreeList_pop() == start_of_free_list) << endl;
        erase(blocks, start_of_free_list);

        cout << "testing adding a new block onto the freelist now" << endl;
        FreeList_append(br);
        blocks.push_back(br);
        sort(blocks.begin(), blocks.end());

        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

        cout << "allocate enough space to be able to free 512 * 2 new blocks" << endl;
        size_t count = 0;
        for (int i = 0; i <= 1; i++) {
                do {
                        block_reference newblk = FreeList_pop();
                        if (newblk == 0) {
                                cerr << "FreeList_pop() failed! error: "
                                     << ((cofs_errno == 0)
                                         ? "free list is empty!"
                                         : strerror(cofs_errno))
                                     << endl;
                                break;
                        }
                        ++count;
                        erase(blocks, newblk);
                } while (blocks.size() % 512 != i);
        }
        cout << "allocated " << count << " new blocks. now running fsck" << endl;
        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());

        cout << "test filling up and allocating a new storage block" << endl;

        srand(time(nullptr));
        // we need to do some nonsense to avoid duplicate elements since the rand gen is with replacement
        unordered_set<block_reference> no_dups;
        no_dups.insert(blocks.cbegin(), blocks.cend());
        do {
                block_reference newblk;
                do {
                        newblk = rand() % number_of_data_blocks + 1;
                } while (no_dups.contains(newblk));

                if (!FreeList_append(newblk)) {
                        cerr << "FreeList_append() failed! error: "
                                << ((cofs_errno == 0)
                                   ? "free list is full!"
                                   : strerror(cofs_errno))
                                << endl;
                        break;
                }
                ++count;
                no_dups.insert(newblk);
                blocks.push_back(newblk);
        } while (blocks.size() % 512 != 0);
        cout << "freed " << count << " data blocks" << endl;

        sort(blocks.begin(), blocks.end());
        FreeList_fsck(sblock_incore.flist_head, blocks.data(), blocks.size());
#endif
        layer0_teardown();
}


int main(int argc, char **argv)
{
        cout << "test hello!\n";
        if (argc > 1) {
                cout << "running mkfs on " << devpath << endl;
                test_callMkfs();
                return 0;
        }
//        cout << "testing freelist creation" << endl;
//        test_fsck();

        cout << endl << "testing freelist push/pop" << endl;
        test_pop();

//	cout << endl << "testing freelist fill up" << endl;
//	test_fill();
	return 0;

        test_retreiveMkfs();

        return 0;
}
