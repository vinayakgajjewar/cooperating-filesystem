//
// Created by ryan on 11/28/23.
//

#pragma once

#include "cofs_data_structures.h"

typedef bool (*datablock_foreach) (block_reference block, void *other);

/**
 * Iterates over each datablock in an inode and calls func(&block,  other) on it
 * @param inode inode whose data we want to examine. should be of type DIR or FILE
 * @param func function to call on each each block (blocknumber passed as a parameter)
 * @param start_block block offset within the file to start at
 * @param stop_on_false setting to `true` will halt the iteration once func() returns false,
 *              otherwise it will continue until all data blocks have been traversed
 * @param other extraneous parameter to allow transferring whatever other data you want
 *      into each func() call
 * @return `true` if every call to `func` returned `true`, else `false`
 */

bool
foreach_datablock_in_inode(cofs_inode *inode, datablock_foreach func, size_t start_block, bool stop_on_false,
                           void *other);

/**
 * Gets a new data block from the freelist and assigns it to inode
 * @param inode the inode to assign the data block
 * @return reference (disk block #) of the newly allocated block on success, else zero
 */
block_reference alloc_new_datablock(cofs_inode *inode);

/**
 * Gets the block number of the last data block in a file
 * @param inode inode of the file
 * @return last block number, or 0 if file is empty
 */
block_reference get_last_datablock(cofs_inode *inode);

/**
 * Puts all of the data blocks after `start` belonging to an inode back onto the freelist
 * @param inode  The inode owning the datablocks
 * @param start  The offset within the inode after which to free blocks. A value of 0 frees all of them
 * @return `true` on success, else `false`
 */
bool release_datablocks(cofs_inode *inode, size_t start);