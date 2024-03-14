/* layer0.h - COFS Layer 0 API
 *
 * Created by ryan on 11/13/23.
*/

#pragma once

#include "cofs_parameters.h"
#include "cofs_data_structures.h"

/**
 * Initializes layer 0. Should not be called more than once.
 * @param blkdev Path to the block device containing our filesystem
 * @param memsize Size of memory to allocate for in-core filesystem
 * @note  If `blkdev` is non-NULL, `memsize` is ignored. Likewise,
 *        if `memsize` is non-zero, `blkdev` must be NULL. This policy
 *        should be enforced by the caller.
 * @return `true` on success, else `false`
 */
bool layer0_init(const char *blkdev, size_t memsize);

/**
 * mmap's the device file at `path` into memory
 * @param path Path of device to mmap
 * @param size outptr to size of block device (NOT size of filesystem)
 * @return pointer to start of mapped region
 */
void *layer0_mapBlkdev(const char *path, size_t *size);

/**
 * Tears down layer 0 in preparation for unmounting
 * @return `true` on success, else `false`
 */
bool layer0_teardown(void);

/**
 * Write `BLOCK_SIZE` bytes from the specified buffer out to the specified disk block
 * @param bnum Disk block number to write
 * @param buf Data to write to the block
 * @return -1 on failure, else 0
 * @note No bounds checking is done--if the region of memory pointed to by buf
 *      is smaller than 1 disk block, this *will* segfault
 */
int layer0_writeBlock(block_reference bnum, const void *buf);

/**
 * Read `BLOCK_SIZE` bytes from the specified disk block number into `buf`
 * @param bnum Disk block number to read
 * @param buf Buffer to read into
 * @return -1 on failure, else 0
 * @note No bounds checking is done--if the region of memory pointed to by buf
 *      is smaller than 1 disk block, this *will* segfault
 */
int layer0_readBlock(block_reference bnum, void *buf);