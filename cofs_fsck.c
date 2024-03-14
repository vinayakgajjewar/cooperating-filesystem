/* cofs_fsck.c - fsck() program wrapper for COFS
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "free_list.h"
#include "layer0.h"
#include "superblock.h"
#include "cofs_parameters.h"
#include "cofs_inode_functions.h"

#define ASSERT_EQ(expected, actual, retvar)                                      \
        do {                                                                     \
                if ((expected) != (actual)) {                                    \
                        fprintf(stderr, "Assertion failed at %s:%d\n"            \
                                        "\texpected: %s\n"                       \
                                        "\tactual: %s\n",                        \
                                __FILE__, __LINE__, #expected, #actual);         \
                        retvar = false;                                          \
                }                                                                \
        } while (0)

typedef block_reference blockvec_t;

typedef struct {
    blockvec_t *data;
    size_t len;
    size_t __size;
} blockvector;

static blockvector blockvector_create(size_t init_size)
{
        blockvector res = {
                .data = calloc(init_size, sizeof(blockvec_t)),
                .len = 0,
                .__size = init_size,
        };

        if (res.data == NULL)
                return (blockvector) {NULL, 0, 0};

        return res;
}

static bool __blockvector_grow(blockvector *vec, size_t newsize)
{
        void *newdata = reallocarray(vec->data, newsize, sizeof(blockvec_t));
        if (newdata == NULL)
                return false;

        vec->data = newdata;
        vec->__size = newsize;
        return true;
}

static void blockvector_pushBack(blockvector *vec, blockvec_t elem)
{
        if (vec->len == vec->__size
            && !__blockvector_grow(vec, vec->len * 2))
        {
                return;
        }

        vec->data[vec->len++] = elem;
}

static void show_usage(bool error)
{
        FILE *out = error ? stderr : stdout;
        fprintf(out, "Usage: cofs_fsck -b <device_path>\n"
                     "       cofs_fsck -m <memsize>\n"
                     "       cofs_fsck -h, --help\n");

        exit(error ? EXIT_FAILURE : EXIT_SUCCESS);
}

static bool fsck_in_mem(size_t memsize);
static bool fsck_on_dev(const char *devpath);

static size_t n_blocks;
static const block_reference superblock_block_number = 0;
static size_t ilist_size_in_blocks;
static block_reference start_of_free_list;
static size_t number_of_data_blocks;

/**
 * Standard main function.
 * @param argc number of arguments (self explanatory)
 * @param argv see USAGE below
 * @return exit status
 */
int main(int argc, char *argv[])
{
        if (argc < 2)
                show_usage(true);

        if (strcmp(argv[1], "-h") == 0
            || strcmp(argv[1], "--help") == 0)
        {
                show_usage(false);
                return 0;
        }

        if (argc != 3) {
                show_usage(true);
                return EXIT_FAILURE;
        }

        // flip the bool argument so `true` (1) returns 0; vice-versa
        if (strcmp(argv[1], "-b") == 0) {
                return !fsck_on_dev(argv[2]);
        } else if (strcmp(argv[1], "-m") == 0) {
                size_t memsz;
                sscanf(argv[2], "%zu", &memsz);
                return !fsck_in_mem(memsz);
        }

        show_usage(true);

        return EXIT_FAILURE;
}

static bool __fsck_check_fs_params(size_t fs_size)
{
        bool ret = true;

        ASSERT_EQ(ilist_size_in_blocks, sblock_incore.ilist_size, ret);
        ASSERT_EQ(number_of_data_blocks, sblock_incore.n_blocks, ret);
        ASSERT_EQ(start_of_free_list, sblock_incore.flist_head, ret);
        ASSERT_EQ(n_blocks, 1 + ilist_size_in_blocks + number_of_data_blocks, ret);

        return ret;
}

typedef bool (*__fsck_datablock_check)(block_reference block, void *other);

// iterate over all levels of data blocks in an inode and call func() on each one
static bool
__fsck_foreach_datablock_in_inode(cofs_inode *inode,
                                  __fsck_datablock_check func,
                                  bool stop_on_false,
                                  void *other)
{
        // treat all inodes as files, since both dirs and files have the same direct/indirect
        // block layout

        bool ret = true;

        /* direct blocks */
        bool foreach_direct_block(block_reference *dir_blocks)
        {
                bool ret = true;
                for (size_t b = 0; b < N_DIRECT_BLOCKS; b++) {
                        block_reference block = dir_blocks[b];
                        if (block == 0)
                                return ret;

                        ret = (*func)(block, other);

                        if (stop_on_false && !ret)
                                return false;
                }

                return ret;
        }

        /* single indirect blocks */
        bool foreach_1indirect_block(block_reference *indir_blocks)
        {
                block_reference *blockbuf = calloc(N_DIRECT_BLOCKS, sizeof(block_reference));
                bool ret = true;
                for (size_t b = 0; b < N_1INDIRECT_BLOCKS; b++) {
                        block_reference block = indir_blocks[b];
                        if (block == 0)
                                goto finish_1indir;

                        layer0_readBlock(block, blockbuf);
                        ret = foreach_direct_block(blockbuf);

                        if (stop_on_false && !ret) {
                                ret = false;
                                goto finish_1indir;
                        }
                }

        finish_1indir:
                free(blockbuf);
                return ret;
        }

        /* double indirect blocks */
        bool foreach_2indirect_block(block_reference *indir2_blocks)
        {
                block_reference *blockbuf = calloc(N_1INDIRECT_BLOCKS, sizeof(block_reference));
                bool ret = true;
                for (size_t b = 0; b < N_2INDIRECT_BLOCKS; b++) {
                        block_reference block = indir2_blocks[b];
                        if (block == 0)
                                goto finish_2indir;

                        layer0_readBlock(block, blockbuf);
                        ret = foreach_1indirect_block(blockbuf);

                        if (stop_on_false && !ret) {
                                ret = false;
                                goto finish_2indir;
                        }
                }

        finish_2indir:
                free(blockbuf);
                return ret;
        }

         /* triple indirect blocks */
        bool foreach_3indirect_block(block_reference *indir3_blocks)
        {
                block_reference *blockbuf = calloc(N_2INDIRECT_BLOCKS, sizeof(block_reference));
                bool ret = true;
                for (size_t b = 0; b < N_3INDIRECT_BLOCKS; b++) {
                        block_reference block = indir3_blocks[b];
                        if (block == 0)
                                goto finish_3indir;

                        layer0_readBlock(block, blockbuf);
                        ret = foreach_2indirect_block(blockbuf);

                        if (stop_on_false && !ret) {
                                ret = false;
                                goto finish_3indir;
                        }
                }

        finish_3indir:
                free(blockbuf);
                return ret;
        }

        cofs_file_inode *datablocks = &inode->file;
        ret = foreach_direct_block(datablocks->direct_blocks) && ret;
        ret = foreach_1indirect_block(datablocks->single_indirect_blocks) && ret;
        ret = foreach_2indirect_block(datablocks->double_indirect_blocks) && ret;
        ret = foreach_3indirect_block(datablocks->triple_indirect_blocks) && ret;

        return ret;
}

bool __fsck_check_data_blocks(block_reference block, void *inum)
{
        if (block >= n_blocks
                    || block < start_of_free_list)
        {
                fprintf(stderr,
                        "fsck: file inode #%zu contains invalid data block #%zu",
                        *(inode_reference *)inum, block);
                return false;
        }
        return true;
}

static bool __fsck_check_inode(cofs_inode *inode, inode_reference inum)
{
        bool ret = true;

        ret = __fsck_foreach_datablock_in_inode(inode, &__fsck_check_data_blocks, false, NULL);

//        bool __check_d_inode(void)
//        {
//                bool ret = true;
//        }

        ASSERT_EQ(0, inode->permissions.as_int & (1<<15), ret);

        return ret;
}

static bool __fsck_check_ilist(size_t fs_size)
{
        printf("fsck: checking ilist\n");
        inode_reference ilist_start = 1;
        /*inode_reference *inodes = calloc(sblock_incore.ilist_size, sizeof(inode_reference));
        if (inodes == NULL) {
                fprintf("fsck: failed to allocate space for ilist\n");
                return false;
        }*/

        bool ret = true;

        /* iterate over the ilist and make sure every alloc'd inode has a valid type field */
        cofs_inode *inode = malloc(sizeof(cofs_inode));
        for (inode_reference inum = 0; inum < ilist_size_in_blocks * INODES_PER_BLOCK; inum++) {
                read_inode(inode, inum);
                ret = __fsck_check_inode(inode, inum);
        }

        printf("\tilist check looks good.\n");
        free(inode);
        return ret;

//        free(inodes);
}

static bool fsck_in_mem(size_t memsize)
{
        bool ret = true;
        ret = layer0_init(NULL, memsize);
        if (!ret)
                goto cleanup;

        n_blocks = memsize / COFS_BLOCK_SIZE;
        ilist_size_in_blocks = n_blocks / ILIST_SIZE_FRACTION;
        start_of_free_list = (ilist_size_in_blocks) + 1;
        number_of_data_blocks = n_blocks - (1 + ilist_size_in_blocks);

        /* check the parameters in the superblock match the memory size */
        if (!__fsck_check_fs_params(memsize)) {
                fprintf(stderr, "fsck: superblock parameter check failed. exiting.\n");
                return false;
        }

        /* check the i-list */
        __fsck_check_ilist(memsize);

        /* build the list of free data blocks by reading allocated inodes */
        blockvector blocks = blockvector_create(16);

        /* check the datablock freelist */
//        FreeList_fsck();

cleanup:
        ret = layer0_teardown();
        return ret;
}

static bool fsck_on_dev(const char *devpath)
{

        return true;
}