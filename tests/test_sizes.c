/*
 * some tests for the data structures in cofs_data_structures.h
 *
 * compile with
 *      gcc -std=gnu17 test_sizes.c -o test_sizes
 */

#include "cofs_data_structures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define PRINT_SIZE(type) \
        printf("size of `" #type "': %zu bytes\n", sizeof(type))

// test reading then writing return data in proper format
void *f1(void *addr, size_t size)
{
        void *res = malloc(size);
        memcpy(res, addr, size);
        return res;
}

int main()
{
        // do we have (relatively) sane sizes?
        PRINT_SIZE(cofs_superblock);
        PRINT_SIZE(cofs_inode);
        PRINT_SIZE(struct timespec);
        PRINT_SIZE(union _inode_permissions);

        // test that we don't muck anything when losing type information
        // by storing/retrieving from memory
        cofs_superblock *sb = malloc(sizeof(cofs_superblock));
        sb->flist_head = 45; // random values
        sb->ilist_size = 32;
        void *test_sb = f1(sb, sizeof(cofs_superblock));
        assert(((cofs_superblock *)test_sb)->flist_head == sb->flist_head);
        assert(((cofs_superblock *)test_sb)->ilist_size == sb->ilist_size);

        cofs_inode in = {
                .type = INODE_TYPE_FILE,
                .permissions = {.group_r = 1, .owner_w = 1, .world_x = 1},
                .in_use = 1,
                .n_bytes = 256,
                .n_blocks = 1,
                .file = {
                        .direct_blocks = {34, 0}, // random block indices LOL
                        // etc
                }
        };
        void *test_in = f1(&in, sizeof in);
        assert( ((cofs_inode *) test_in)->type == INODE_TYPE_FILE);
        assert( ((cofs_inode *) test_in)->permissions.as_int == in.permissions.as_int);
        assert( ((cofs_inode *) test_in)->in_use == in.in_use);
        assert( ((cofs_inode *) test_in)->n_bytes == in.n_bytes);
        assert( ((cofs_inode *) test_in)->file.direct_blocks[0] == in.file.direct_blocks[0]);

// notice that this does NOT include the \0 terminator!
#define TEST_SYMLINK_PATH       ((char []){'/', 'e', 't', 'c', '/', 't', 'e', 's', 't'})
_Static_assert(sizeof TEST_SYMLINK_PATH <= 16);

        cofs_inode symlink = {
                .type = INODE_TYPE_SYML,
                .permissions = {.as_int = 0777}, // rwxrwxrwx
                .in_use = 1,
                .n_bytes = sizeof(TEST_SYMLINK_PATH),
                .syml = {
                        .source_path = TEST_SYMLINK_PATH,
                }
        };
        void *test_sym = f1(&symlink, sizeof symlink);
        assert( ((cofs_inode *) test_sym)->type == INODE_TYPE_SYML);
        assert( ((cofs_inode *) test_sym)->permissions.as_int == symlink.permissions.as_int);
        assert( ((cofs_inode *) test_sym)->in_use == symlink.in_use);
        assert( ((cofs_inode *) test_sym)->n_bytes == symlink.n_bytes);
        assert(strncmp(
                ((cofs_inode *) test_sym)->syml.source_path,
                        symlink.syml.source_path,
                        symlink.n_bytes) == 0);

        printf("All tests passed!\n");

        free(sb);
        free(test_sb);
        free(test_in);

	return 0;
}
