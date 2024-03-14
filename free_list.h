/* free_list.h - block free list for COFS
 *
 */

#pragma once

#include "cofs_data_structures.h"

/**
 * Creates the Free List for data blocks. Should only be called by mkfs.
 * @param n_data_blocks Total number of data blocks in the FS
 * @param head Initial head of the free list
 */
bool FreeList_create(size_t n_data_blocks, block_reference head);

/**
 * Initializes the Free List interface. Should be called as part of the mount
 * process for an existing FS.
 * @param head The head of the free list
 * @return `true` on success, else `false`
 */
bool FreeList_init(block_reference head);

/**
 * Removes the next free block in the free list.
 * @return The index of the removed block
 */
block_reference FreeList_pop(void);

/**
 * Adds the specified block to the free list
 * @param blk - The block number to add to the list
 * @return `true` on success, else `false`
 */
bool FreeList_append(block_reference block_index);

/**
 * Verifies the integrity of the Free List a. la. `fsck(8)`
 * @param head The head of the free list stored in the superblock
 * @param free_blocks An array containing the indices of every free block in the FS
 * @param n_freeblocks The size of the `free_blocks` array
 * @note A complete `fsck.cofs` implementation should construct the `free_blocks` array
 *        by reading which data blocks are allocated from the FS's inodes before calling this
 *        function.
 * @return `true` if intact, else `false`
 */
bool FreeList_fsck(block_reference head, block_reference free_blocks[], size_t n_freeblocks);
