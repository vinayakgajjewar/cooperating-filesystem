/* superblock.c - COFS superblock
 *
 */

#include "superblock.h"
#include "cofs_data_structures.h"
#include "layer0.h"

cofs_superblock sblock_incore;

const unsigned char ZERO_BLOCK[COFS_BLOCK_SIZE] = {0};

int update_superblock(void)
{
        return layer0_writeBlock(0, &sblock_incore);
}