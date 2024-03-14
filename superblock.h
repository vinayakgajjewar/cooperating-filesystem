/* superblock.h - COFS superblock global instance
 *
 */

#pragma once

#include "cofs_data_structures.h"

/**
 * In-core copy of the COFS superblock, so we don't have to load it from disk
 * on every update.
 */
extern cofs_superblock sblock_incore;
extern const unsigned char ZERO_BLOCK[COFS_BLOCK_SIZE];
int update_superblock(void);
