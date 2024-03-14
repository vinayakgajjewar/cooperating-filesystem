/* cofs_inode_functions.c - - COFS implementation for function that allocate, free, read, and write inodes
* 
*
*/
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cofs_inode_functions.h"
#include "layer0.h"
#include "superblock.h"
#include "cofs_errno.h"
#include "layer2.h"

#define ILIST_START_BLOCK       (1UL)

static cofs_inode inode_cache[INODES_PER_BLOCK];
static block_reference cached_inode_block = 0;

bool ilist_create(size_t ilist_size)
{
        memset(&inode_cache, 0, sizeof(inode_cache));

        for (block_reference iblock_num = 0; iblock_num < ilist_size; iblock_num++) {
                for (size_t i = 0; i < INODES_PER_BLOCK; i++)
                        inode_cache[i].inum = iblock_num * INODES_PER_BLOCK + i;

                // add one because the ilist starts at block 1
                if (layer0_writeBlock(iblock_num + ILIST_START_BLOCK, inode_cache) == -1)
                        return false;
        }

        return true;
}

inode_reference allocate_inode() {
    // Calculate size of the ilist in blocks and the number of inodes per block
    size_t ilist_size_in_blocks = sblock_incore.ilist_size;

    // check if our cached block has any free inodes
    if (cached_inode_block != 0) {
        for (size_t i = 0; i < INODES_PER_BLOCK; i++)
            if (!inode_cache[i].in_use) {
                inode_cache[i].in_use = 1;
                layer0_writeBlock(cached_inode_block, inode_cache);
                --sblock_incore.free_inodes;
                return inode_cache[i].inum;
            }
    }

    // Loop through ilist blocks
    for (cached_inode_block = ILIST_START_BLOCK; cached_inode_block <= ilist_size_in_blocks; cached_inode_block++) {
        // Read the block containing inodes
        layer0_readBlock(cached_inode_block, inode_cache);

        // Loop through inodes in the block
        for (inode_reference inode_index = 0; inode_index < INODES_PER_BLOCK; inode_index++) {
            // Check if the inode is free
            if (inode_cache[inode_index].in_use == 0) {
                // Allocate the inode
                inode_cache[inode_index].in_use = 1;

                // Write the updated block back to disk
                layer0_writeBlock(cached_inode_block, inode_cache);

                --sblock_incore.free_inodes;
                // Calculate and return the index of the inode in the ilist
                return inode_cache[inode_index].inum;
            }
        }
    }

    return INODE_MISSING;
}

bool free_inode(inode_reference index) {
    // Calculate the block index and inode index within the block
    size_t block_index = index / INODES_PER_BLOCK + ILIST_START_BLOCK;
    inode_reference inode_index_within_block = index % INODES_PER_BLOCK;

    // read the inode's block into our cache if needed
    if (cached_inode_block != block_index) {
        if (layer0_readBlock(block_index, inode_cache) == -1)
            return false; // read failed
        cached_inode_block = block_index;
    }

    // Mark the inode as free and zero it out
    memset(&inode_cache[inode_index_within_block], 0, sizeof(cofs_inode));
    inode_cache[inode_index_within_block].inum = index;


    // Write the updated inode block back to the disk
    if (layer0_writeBlock(cached_inode_block, inode_cache) == 0) {
            ++sblock_incore.free_inodes;
            return true;
    }
    return false;
}

bool read_inode(cofs_inode* inode, inode_reference index) {
    // Ensure the provided pointer is not NULL
    if (inode == NULL) {
        COFS_ERROR(EIO);
    }

    // Calculate the block index and inode index within the block
    size_t block_index = index / INODES_PER_BLOCK + ILIST_START_BLOCK;
    size_t inode_index_within_block = index % INODES_PER_BLOCK;

    if (cached_inode_block != block_index) {
        // Read the block containing the inode from the disk
        if (layer0_readBlock(block_index, inode_cache) == -1) {
            // Handle read error
            return false;
        }
        cached_inode_block = block_index;
    }

    // Copy the inode data from the block buffer to the provided inode pointer
    memcpy(inode, &inode_cache[inode_index_within_block], INODE_SIZE);

    return true;
}

bool write_inode(cofs_inode* inode, inode_reference index) {
    // Ensure the provided inode pointer is not NULL
    if (inode == NULL) {
        COFS_ERROR(EIO);
    }

    // Calculate the block index and inode index within the block
    size_t block_index = index / INODES_PER_BLOCK + ILIST_START_BLOCK;
    size_t inode_index_within_block = index % INODES_PER_BLOCK;

    if (cached_inode_block != block_index) {
        // Read the block containing the target inode from the disk
        if (layer0_readBlock(block_index, inode_cache) == -1) {
            // Handle read error
            return false;
        }

        cached_inode_block = block_index;
    }

    // Update the specific inode in the block buffer
    memcpy(&inode_cache[inode_index_within_block], inode, INODE_SIZE);

    // Write the updated block back to the disk
    return layer0_writeBlock(cached_inode_block, inode_cache) == 0;
}