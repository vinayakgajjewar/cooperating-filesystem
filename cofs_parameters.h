/* cofs_parameters.h - Parameters for COFS
 *
 * Created by ryan on 11/13/23.
*/

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define COFS_BLOCK_SIZE (1 << 12)
#define DISK_SIZE (COFS_BLOCK_SIZE * NUM_BLOCKS)
#define ILIST_SIZE_FRACTION 10

extern size_t NUM_BLOCKS; // controlled by layer 1