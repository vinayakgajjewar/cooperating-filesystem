// cofs-mkfs.c
// This file contains the mkfs command, which:
// 1. Creates the super block
// 2. Creates the ilist
// 3. Creates the free data block list

#include "cofs_mkfs.h"

#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "layer0.h"
#include "cofs_data_structures.h"
#include "free_list.h"
#include "superblock.h"
#include "cofs_inode_functions.h"
#include "cofs_errno.h"

#define ROOTDIR_INUM    0

static cofs_inode root_dir = {
       .in_use = 1, .type = INODE_TYPE_DIR,
       .permissions = {.world_r = 1, .group_r = 1, .owner_r = 1,
                       .world_w = 1, .group_w = 1, .owner_w = 1,
                       .world_x = 1, .group_x = 1, .owner_x = 1},
       .uid = -1, .gid = -1, // set at runtime
       .n_blocks = 1, .n_bytes = COFS_BLOCK_SIZE, .num_direntries = 2,
       .refcount = 2, .inum = ROOTDIR_INUM,
};

static cofs_direntry root_direntries[DIRENTRIES_PER_BLOCK] = {
        {.inum = ROOTDIR_INUM, .base_name = "."},
        {.inum = ROOTDIR_INUM, .base_name = ".."},
        {{0}, 0}
};

#ifdef BUILD_MKFS_PROGRAM
#include <pwd.h>

static uid_t __fs_uid;
static gid_t __fs_gid;

static inline uid_t fs_uid(void)
{ return __fs_uid; }

static inline gid_t fs_gid(void)
{ return __fs_gid; }

static int exitcode = EXIT_FAILURE;

// returm index of the blkdev option
static size_t mkfs_argparse(int argc, char **argv)
{
        __fs_uid = getuid();
        __fs_gid = getgid();
        struct passwd *p;
        char opt;
        while ((opt = getopt(argc, argv, "ho:g:")) != -1) {
                switch (opt) {
                    case 'o': {
                        p = getpwnam(optarg);
                        if (p == NULL) {
                            fprintf(stderr, "Invalid user '%s'\n", optarg);
                            exit(EXIT_FAILURE);
                        }
                        __fs_uid = p->pw_uid;
                        break;
                    }

                    case 'g': {
                        p = getpwnam(optarg);
                        if (p == NULL) {
                                fprintf(stderr, "Invalid group '%s'\n", optarg);
                                exit(EXIT_FAILURE);
                        }
                        __fs_gid = p->pw_gid;
                        break;
                    }

                    case 'h':
                        exitcode = EXIT_SUCCESS;
                        return false;

                    case '?':
                    default:
                        return 0;
                }
        }

        /* check for the blkdev option */
        if (optind + 1 != argc)
                return 0;

        return optind;
}

int main(int argc, char* argv[])
{
        // Check if we have valid arguments. If not, enlighten the user.
        size_t blkidx = mkfs_argparse(argc, argv);
        if (!blkidx) {
                fprintf(stderr, "Usage: mkfs.cofs [-o <owner>] [-g <group>] <device path>\n");
                return exitcode;
        }

        // open the block device
        size_t size;
        if (layer0_mapBlkdev(argv[blkidx], &size) == NULL) {
                fprintf(stderr, "Unable to open block device '%s': %s\n",
                        argv[blkidx], strerror(errno));
                return EXIT_FAILURE;
        }

        // Call our mkfs function.
        if (!mkfs(size)) {
                fprintf(stderr, "mkfs.cofs failed for block device '%s\n",
                        argv[blkidx]);
                return EXIT_FAILURE;
        }

        return 0;
}
#else
// default to the PID's owner/group when running as part of the in-mem initialization
static uid_t (*fs_uid) (void) = getuid;
static gid_t (*fs_gid) (void) = getgid;

#endif

__attribute__((weak)) // some tests need to recompile this from source
bool mkfs(size_t disk_size_in_bytes) {
        // Set return value to success
        bool ret = true;

        // Size of disk in blocks
        NUM_BLOCKS = disk_size_in_bytes / COFS_BLOCK_SIZE;

        // Set block numbers for superblock, ilist (10% of disk), and free list
        block_reference const superblock_block_number = 0;
        size_t const ilist_size_in_blocks = NUM_BLOCKS / ILIST_SIZE_FRACTION;
        block_reference const start_of_free_list = (ilist_size_in_blocks) + 1;

        // Set number of data blocks to (total number of blocks on disk - (superblock + blocks in list))
        size_t const number_of_data_blocks = NUM_BLOCKS - (1 + ilist_size_in_blocks);

        // Create a superblock.
        memset(&sblock_incore, 0, sizeof sblock_incore);
        sblock_incore.ilist_size = ilist_size_in_blocks;
        sblock_incore.n_blocks = NUM_BLOCKS;
        sblock_incore.flist_head = start_of_free_list;
        sblock_incore.free_blocks = number_of_data_blocks;
        sblock_incore.free_inodes = ilist_size_in_blocks * INODES_PER_BLOCK;

        // Write our superblock to the device.
        if (update_superblock() != 0)
                return false;

        printf("mkfs.cofs: Wrote superblock of size %lu bytes (1 block)\n", sizeof(cofs_superblock));

        // create and write the i-list
        if (!ilist_create(ilist_size_in_blocks)) {
                fprintf(stderr, "mkfs.cofs: Error in i-list creation: %s\n",
                        strerror(cofs_errno));
                return false;
        }

        printf("mkfs.cofs: Wrote free inode list of size %lu (%lu blocks)\n",
               INODES_PER_BLOCK * ilist_size_in_blocks,
               ilist_size_in_blocks);

        FreeList_create(number_of_data_blocks, start_of_free_list);
        FreeList_init(sblock_incore.flist_head);

#ifndef COFS_TEST_FREELIST
        /* create and write the root directory */
        if (clock_gettime(CLOCK_REALTIME, &root_dir.btim) == -1
            || clock_gettime(CLOCK_REALTIME, &root_dir.mtim) == -1
            || clock_gettime(CLOCK_REALTIME, &root_dir.atim) == -1
            || clock_gettime(CLOCK_REALTIME, &root_dir.ctim) == -1)
        {
                fprintf(stderr, "mkfs.cofs: Unable to record current clock time: %s\n",
                        strerror(errno));
                return false;
        }
        root_dir.inum = allocate_inode();
        if (root_dir.inum == SIZE_MAX) {
                fprintf(stderr, "mkfs.cofs: Unable to allocate inode for root directory: %s\n",
                        strerror(cofs_errno));
                return false;
        }

        root_dir.uid = fs_uid();
        root_dir.gid = fs_gid();

        block_reference rootdir_data = FreeList_pop();
        if (rootdir_data == 0) { // TODO: add errno handling to FreeList
                fprintf(stderr, "mkfs.cofs: Unable to create root directory: %s\n",
                        strerror(cofs_errno));
                return false;
        }

        // should always be 0, but just to be safe:
        root_direntries[0].inum = root_dir.inum;
        root_direntries[1].inum = root_dir.inum;
        root_dir.dir.direct_blocks[0] = rootdir_data;
        if (layer0_writeBlock(rootdir_data, &root_direntries) == -1) {
                fprintf(stderr, "mkfs.cofs: Unable to write root directory contents: %s\n",
                        strerror(cofs_errno));
                return false;
        }

        if (!write_inode(&root_dir, root_dir.inum)) {
                fprintf(stderr, "mkfs.cofs: Unable to write inode to disk: %s\n",
                        strerror(cofs_errno));
                return false;
        }

        sblock_incore.root_dir = root_dir.inum;
#endif
        update_superblock();

        printf("mkfs.cofs: Initialized free block list starting at block %lu\n", start_of_free_list);

        return ret;
}
