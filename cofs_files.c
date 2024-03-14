/* cofs_files.c - file-related functions for COFS
 *
 */

#include "cofs_files.h"

#include <string.h>
#include <errno.h>
#include <assert.h>

#include "cofs_inode_functions.h"
#include "free_list.h"
#include "layer0.h"
#include "cofs_datablocks.h"
#include "layer2.h"
#include "cofs_errno.h"

static unsigned char cached_block[COFS_BLOCK_SIZE];
static block_reference cached_idx = 0;

static inline size_t intdiv_ceil(size_t dividend, size_t divisor)
{
        // NOTE: the addition *can* overflow if numbers are large enough, but
        // we're not currently using it in any scenario where that could occur
        return (dividend + divisor - 1) / divisor;
}

struct __fileRead_Args {
    char *const buf;
    size_t first_offset;
    const size_t length;
    size_t bytes_read;
    const size_t file_size;
};

static bool __fileRead_Iterator(block_reference blk, void *_args)
{
        struct __fileRead_Args *args = _args;

        if (args->bytes_read >= args->length)
                return false; // done reading--stop iteration

        // sanity check
        assert(args->bytes_read <= args->file_size);

        if (layer0_readBlock(blk, cached_block) == -1)
                return false;

        size_t start = 0;
        size_t amnt = COFS_BLOCK_SIZE;

        if (args->bytes_read == 0)
                start = args->first_offset;
        if (args->length - args->bytes_read < COFS_BLOCK_SIZE)
                amnt = args->length - args->bytes_read;
        if (args->bytes_read + amnt > args->file_size)
                amnt = args->file_size - args->bytes_read;

        memcpy(args->buf + args->bytes_read, cached_block + start, amnt);

        args->bytes_read += amnt;

        return true;
}

bool File_readData(cofs_inode *file, char *buf, off_t start, size_t length)
{
        if (buf == NULL)
                COFS_ERROR(EFAULT);

        // Calculate initial block number and offset within the block
        size_t block_index = start / COFS_BLOCK_SIZE;
        size_t block_offset = start % COFS_BLOCK_SIZE;

        struct __fileRead_Args args = {buf, block_offset, length, 0, file->n_bytes};
        foreach_datablock_in_inode(file, &__fileRead_Iterator, block_index, true, &args);

        // technically this code is redundant but it's good to have explicitly
        if (args.bytes_read < length && cofs_errno == 0)
                return false; // EOF

        return cofs_errno == 0;
}

struct __fileWrite_Args {
    const char *const buf;
    size_t first_offset;
    const size_t length;
    size_t bytes_written;
    const size_t final_size;
};

static bool __fileWrite_Iterator(block_reference blk, void *_args)
{
        struct __fileWrite_Args *args = _args;

        if (args->bytes_written >= args->length)
                return false; // done writing--stop iteration

       // sanity check
        assert(args->bytes_written <= args->final_size);

        size_t start = 0;
        size_t amt = COFS_BLOCK_SIZE;

        if (args->bytes_written == 0)
                start = args->first_offset;
        if (args->length - args->bytes_written < COFS_BLOCK_SIZE)
                amt = args->length - args->bytes_written;
        if (args->length - args->bytes_written > args->final_size)
                amt = args->final_size - args->bytes_written;

        if (start != 0) {
                if (layer0_readBlock(blk, cached_block) == -1)
                        return false;
        } else {
                memset(cached_block, 0, COFS_BLOCK_SIZE);
        }

        memcpy(cached_block + start, args->buf + args->bytes_written, amt);


        if (layer0_writeBlock(blk, cached_block) == -1)
                return false;

        args->bytes_written += amt;

        return true;
}

bool File_writeData(cofs_inode *file, const char *buf, off_t start, size_t length)
{
        if (buf == NULL)
                COFS_ERROR(EFAULT);

        // Calculate initial block number and offset within the block
        size_t block_index = start / COFS_BLOCK_SIZE;
        size_t block_offset = start % COFS_BLOCK_SIZE;

        // calculate ending file size if the write succeeds
        size_t initial_size = file->n_bytes;
        size_t final_size = start + length;
        while (file->n_blocks < intdiv_ceil(final_size, COFS_BLOCK_SIZE))
                alloc_new_datablock(file);

        struct __fileWrite_Args args = {buf, block_offset, length, 0, final_size};
        foreach_datablock_in_inode(file, &__fileWrite_Iterator, block_index, true, &args);

        // don't grow the file if the write didn't fully complete
        if (cofs_errno != 0 && file->n_bytes < final_size)
                release_datablocks(file, intdiv_ceil(file->n_bytes, COFS_BLOCK_SIZE));
        else
                file->n_bytes += args.bytes_written;

        return cofs_errno == 0;
}