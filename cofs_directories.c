/* cofs_directories.h - directory-related functions for COFS
 *
 */

#include "cofs_directories.h"

#include <string.h>
#include <assert.h>

#include "cofs_inode_functions.h"
#include "free_list.h"
#include "layer0.h"
#include "cofs_datablocks.h"
#include "layer2.h"
#include "cofs_errno.h"
#include "cofs_util.h"

static cofs_direntry block_cache[DIRENTRIES_PER_BLOCK];
static block_reference cached_idx = 0;

static bool __getNextUnused_Iterator(block_reference blk, void *found_entry)
{
        if (layer0_readBlock(blk, block_cache) == -1)
                return false;

        cached_idx = blk;

        for (size_t entry = 0; entry < DIRENTRIES_PER_BLOCK; entry++) {
                if (block_cache[entry].base_name[0] == '\0') {
                        *((cofs_direntry **) found_entry) = &block_cache[entry];
                        return false; // false will stop the iteration
                }
        }

        return true;
}

// returns a pointer into the block_cache referencing the next unsused directory entry
// in the directory dir, else NULL if no more are available
static cofs_direntry *__get_next_unused(cofs_inode *dir)
{
        cofs_direntry *found_entry = NULL;
        if (foreach_datablock_in_inode(dir, &__getNextUnused_Iterator, 0, true, &found_entry)) {
                // not found -- need to allocate new block
                block_reference block = alloc_new_datablock(dir);
                if (block == 0)
                        return NULL;

                dir->n_bytes += COFS_BLOCK_SIZE;

                if (layer0_readBlock(block, &block_cache) == -1)
                        return NULL;

                // read block into cache and return
                cached_idx = block;
                return &block_cache[0];
        }

        return found_entry;
}

// adds a directory entry without writing it back to disk. BE CAREFUL -- calling this
// multiple times without flushing to disk can result in some blocks not being written *EVER*
// if successive entries are not all allocated from the same data block!
static bool __addEntry_noWriteBack(cofs_inode *dir, const char *name, inode_reference inum)
{
        cofs_direntry *new = __get_next_unused(dir);
        if (new == NULL)
                return false;

        if (strlen(name) + 1 > MAX_FILE_BASENAME)
                COFS_ERROR(ENAMETOOLONG);

        strcpy(new->base_name, name);
        new->inum = inum;
        ++dir->num_direntries;
        return true;
}

bool Dir_addEntry(cofs_inode *dir, const char *name, inode_reference inum)
{
        return __addEntry_noWriteBack(dir, name, inum)
                && (layer0_writeBlock(cached_idx, block_cache) == 0)
                && update_inode_mtime(dir)
                && update_inode_ctime(dir)
                && (write_inode(dir, dir->inum));
}

bool Dir_create(cofs_inode *dir, cofs_inode *parent)
{
        /* we only worry about poopulating the directory portion of the inode; everything
         * else can be done by a caller. also have the caller populate the parent
         */

        dir->num_direntries = 0;
        dir->in_use = 1;
        dir->type = INODE_TYPE_DIR;
        // safe to use here since we know these will be the 1st two entries in the dir
        if (!Dir_addEntry(dir, ".", dir->inum)
            || !Dir_addEntry(dir, "..", parent->inum)
            || (layer0_writeBlock(cached_idx, block_cache) == -1))
        {
                return false;
        }

        // apparently the '.' and '..' listings count as links to directories
        ++parent->refcount;
        ++dir->refcount;

        return true;
}

struct __dirLookUpArgs {
    const char *target_name; // INPUT
    inode_reference inum; // OUTPUT
    bool remove_entry; // INPUT
    size_t entries_to_search; // INTERNAL STATE
};

static bool __dirLookup_Iterator(block_reference block, void *_args)
{
        struct __dirLookUpArgs *args = _args;
        const char *target_name = ((struct __dirLookUpArgs *) _args)->target_name;
        bool remove_entry = ((struct __dirLookUpArgs *) _args)->remove_entry;

        if (layer0_readBlock(block, block_cache) == -1)
                return false;

        cached_idx = block;
        for (size_t entry = 0; entry < DIRENTRIES_PER_BLOCK; entry++) {
                if (strcmp(block_cache[entry].base_name, target_name) == 0) {
                        ((struct __dirLookUpArgs *) _args)->inum = block_cache[entry].inum;
                        if (remove_entry) {
                                memset(block_cache[entry].base_name, '\0', MAX_FILE_BASENAME);
                                block_cache[entry].inum = 0;
                                layer0_writeBlock(block, block_cache);
                        }
                        return false; // false will stop the iteration
                }

                if (--args->entries_to_search == 0)
                        COFS_ERROR(ENOENT);
        }

        return true;
}

inode_reference Dir_lookup(cofs_inode *dir, const char *name)
{
        struct __dirLookUpArgs lookup_args = {name, INODE_MISSING, false, dir->num_direntries};
        if (foreach_datablock_in_inode(dir, &__dirLookup_Iterator, 0, true, &lookup_args)) {
                if (cofs_errno == 0)
                        cofs_errno = ENOENT;
                return INODE_MISSING;
        }

        return lookup_args.inum;
}

bool Dir_removeEntry(cofs_inode *dir, const char *name)
{
        PRINT_DBG("removing entry %s\n", name);
        struct __dirLookUpArgs lookup_args = {name, INODE_MISSING, true, dir->num_direntries};
        if (foreach_datablock_in_inode(dir, &__dirLookup_Iterator, 0, true, &lookup_args)) {
                if (cofs_errno == 0)
                        cofs_errno = ENOENT;
                return false;
        }

        dir->n_bytes -= sizeof(cofs_direntry);

        cofs_inode bye;
        if (!read_inode(&bye, lookup_args.inum))
                return false;

        if (!decrement_inode_refcount(&bye))
                return false;

        if (!update_inode_mtime(dir) || !update_inode_ctime(dir))
                COFS_ERROR(errno);

        // TODO: deallocate data blocks if necessary

        --dir->num_direntries;
        assert(dir->num_direntries != SIZE_MAX); // overflow

        return write_inode(dir, dir->inum);
}
