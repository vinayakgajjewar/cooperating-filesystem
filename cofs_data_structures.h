/* cofs_data_structures.h - Filesystem data structures for COFS
 *
 */

#pragma once

#include <sys/stat.h>

#include "cofs_parameters.h"

/*
 * We need
 *      - inode
 *              - access permissions
 *              - owner/group
 *              - access time/modify time/inode change time
 *              - inode type: file/directory/device/symlink
 *              - N direct blocks
 *              - however many single/double/triple indirect blocks
 *              - size in bytes
 *              - size in blocks
 *              - ref count (# of hardlinks to file)
 *              - free bit (is on free list)
 *
 *      - data block (should be integer multiple of inode size)
 *              - could just be a typedef/singleton struct
 *
 *      - superblock (size == BLKSIZE)
 *              - # of blocks in i-list
 *              - total # of blocks in partition (including super block itself)
 *              - pointer to free list head
 *              OPTIONAL (from Linux kernel docs)
 *              - when fs was last mounted
 *              - was fs cleanly unmounted
 *              - when was it modified
 *              - version???
 *
 */

#define N_DIRECT_BLOCKS         12U
#define N_1INDIRECT_BLOCKS      3U
#define N_2INDIRECT_BLOCKS      1U
#define N_3INDIRECT_BLOCKS      1U

#define INODE_SIZE              (256UL)
#define INODES_PER_BLOCK        (COFS_BLOCK_SIZE / INODE_SIZE)
_Static_assert(INODE_SIZE * INODES_PER_BLOCK == COFS_BLOCK_SIZE,
                "block size must be a multiple of inode size");

#define MAX_FILE_BASENAME       (256UL - sizeof(inode_reference))

#define INODE_TYPE_FILE         (0b00)
#define INODE_TYPE_DIR          (0b01)
#define INODE_TYPE_SPEC         (0b10)
#define INODE_TYPE_SYML         (0b11)

typedef size_t  inode_reference;
typedef size_t  block_reference;
#define BLOCKS_PER_INDIRECT     (COFS_BLOCK_SIZE / sizeof(block_reference))
#define COFS_MAX_FILEBLOCKS     \
        (N_DIRECT_BLOCKS + N_1INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT \
          + N_2INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT \
          + N_3INDIRECT_BLOCKS * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT)

#define COFS_MAX_FILESIZE       (COFS_MAX_FILEBLOCKS * COFS_BLOCK_SIZE)

/** If we have a permission value that is e.g.
 *      _inode_permissions mode = { ... };
 * Then to retrieve e.g. owner's execute permission from a given inode_perms value, use
 *      bool owner_execute = mode.owner_x;
 * To set e.g. world's read and execute permission:
 *      _inode_permissions mode = {.world_r = 1, .world_x = 1};
 *  or
 *      mode.world_r = mode.world_x = 1;
 * To retreive the permission value as a plain integer (for e.g. comparisons), use
 *     uint16_t my_int = mode.as_int;
 */
typedef union _inode_permissions_u {
    struct
    {
        uint8_t world_x: 1, /* execute */
        world_w: 1, /* write */
        world_r: 1; /* read */

        uint8_t group_x: 1, /* execute */
        group_w: 1, /* write */
        group_r: 1; /* read */

        uint8_t owner_x: 1, /* execute */
        owner_w: 1, /* write */
        owner_r: 1; /* read */

        // from https://unix.stackexchange.com/a/447346
        uint8_t t: 1,  /* sticky bit */
        sg: 1,  /* setgid */
        su: 1;  /* setuid */
    } __attribute((packed));
    uint16_t as_int: 15;
} __attribute((packed)) inode_permissions;

/* The FILE inode!! */
typedef struct _cofs_file_inode_s {
        /* these should be plain data blocks */
        block_reference direct_blocks[N_DIRECT_BLOCKS];
        block_reference single_indirect_blocks[N_1INDIRECT_BLOCKS];
        block_reference double_indirect_blocks[N_2INDIRECT_BLOCKS];
        block_reference triple_indirect_blocks[N_3INDIRECT_BLOCKS];
} cofs_file_inode;

/* Type of a single directory entry */
typedef struct _cofs_direntry_s {
    // ideally should fit evenly into one block
    char base_name[MAX_FILE_BASENAME];
    inode_reference inum;
} cofs_direntry;
_Static_assert(COFS_BLOCK_SIZE % sizeof(cofs_direntry) == 0);
#define DIRENTRIES_PER_BLOCK    (COFS_BLOCK_SIZE / sizeof(cofs_direntry))

/* The DIRECTORY inode!! */
typedef struct _cofs_dir_inode_s {
        /* these blocks should be full of `cofs_direntry` variables */
        block_reference direct_blocks[N_DIRECT_BLOCKS];
        block_reference single_indirect_blocks[N_1INDIRECT_BLOCKS];
        block_reference double_indirect_blocks[N_2INDIRECT_BLOCKS];
        block_reference triple_indirect_blocks[N_3INDIRECT_BLOCKS];
} cofs_dir_inode;

/* The SPECIAL inode!! */
typedef struct _cofs_spec_inode_s {
        /* TODO: verify if these are correct */
        int device_type;
        int device_number;
} cofs_spec_inode;

#define SYML_SOURCE_PATHLEN    \
        (sizeof(cofs_file_inode) - N_DIRECT_BLOCKS * sizeof(block_reference))
/* The SYMLINK inode!! */
typedef struct _cofs_syml_inode_s {
    block_reference direct[N_DIRECT_BLOCKS];
    /* small source paths (<16 chars) are stored in `source_path`,
         * otherwise in the direct blocks.
         *
         * Note that the n_bytes field shoud be the number of characters
         * in the source_path string.
         */
        char source_path[SYML_SOURCE_PATHLEN];
} cofs_syml_inode;

/**
 * Top-level inode type, able to represent a generic inode.
 * Hopefully usage is straightforward, but see file test_sizes.c
 * for example code if not.
 */
typedef struct cofs_inode_s {
        /* Note: everything up to and until the `refcount` field is identical
         * in all four inode types. After that, things differ.
         */
        uint8_t in_use: 1,                /* 0 if FREE, 1 if used */
                type: 2;                /* MUST match INODE_TYPE */

         /* permissions for symlinks are ALWAYS rwxrwxrwx.
         * Note that this doesn't necessarily mean that the file they point to
         * has those same permissions!
         */
        union _inode_permissions_u permissions; // for symlinks, assign this as = { .as_int = 0777 };

        uid_t uid;
        gid_t gid;

        struct timespec atim;		/* Time of last access.  */
        struct timespec mtim;		/* Time of last modification.  */
        struct timespec ctim;           /* Time of last status change.  */
        struct timespec btim;           /* Time of creation (birth). */

        /* size of inode's data in blocks and bytes. both are allowed to be 0 for newly
         * created files.
         */
        size_t n_bytes;
        size_t n_blocks;

        /* apparently you can hardlink a symbolic link. who knew?
         * This field applies to all types except directories, for which
         * it should always be 1
         */
        size_t refcount;

        /* the inode's number in the i-list. Makes things a lot easier */
        inode_reference inum;

        /* number of entries in the directory (since we can have holes) */
        size_t num_direntries;

        /* here is where things differ */
    union {
        cofs_file_inode file;
        cofs_dir_inode  dir;
        cofs_spec_inode dev;
        cofs_syml_inode syml;
    };
} __attribute__((aligned(INODE_SIZE))) cofs_inode;
_Static_assert(sizeof(cofs_inode) == INODE_SIZE,
                "inode type too large! update `INODE_SIZE` or make it smaller!");

typedef struct {
        size_t          ilist_size;      /* # of blocks in ilist */
        size_t          n_blocks;        /* total # blocks in partition */
        size_t          flist_head;      /* (data block) free list head */
        inode_reference root_dir;        /* inode containing the FS's root directory */
        // other stuff?

        /* non-essential information used to populate the struct statvfs.
         * not guaranteed to be fully up-to-date:
         *      do not use for anything Important!
         */
        size_t          free_blocks;
        size_t          free_inodes;
} __attribute__((aligned(COFS_BLOCK_SIZE))) cofs_superblock;

_Static_assert(sizeof(cofs_superblock) == COFS_BLOCK_SIZE);
