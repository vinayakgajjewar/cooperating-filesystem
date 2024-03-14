/* layer2.c - Layer 2 interface functions that aren't syscalls
 */

#include "layer2.h"

#include <stdlib.h>
#include <time.h>
#include <libgen.h>
#include <string.h>

#include "cofs_directories.h"
#include "cofs_inode_functions.h"
#include "superblock.h"
#include "cofs_datablocks.h"
#include "cofs_errno.h"

void fill_statbuf(struct stat *statbuf, const cofs_inode *inode)
{
        struct stat out = {
                .st_ino = inode->inum, .st_nlink = inode->refcount,
                .st_mode = get_st_mode(inode),
                .st_uid = inode->uid, .st_gid = inode->gid,
                .st_size = inode->n_bytes, .st_blocks = inode->n_blocks,
                .st_atim = inode->atim, .st_mtim = inode->mtim, .st_ctim = inode->ctim,
                .st_blksize = COFS_BLOCK_SIZE,
        };

        memcpy(statbuf, &out, sizeof(struct stat));
}

// TODO: remove permission check code once verified that FUSE does it all properly
inode_reference namei(const char *pathname)
{
        if (strcmp(pathname, "/") == 0)
                return sblock_incore.root_dir;

        inode_reference parent = namei_parent(pathname);
        if (parent == INODE_MISSING)
                return INODE_MISSING;

        cofs_inode inode;
        if (!read_inode(&inode, parent))
                return INODE_MISSING;

        const char *dirent = gnu_basename(pathname);
        return Dir_lookup(&inode, dirent);
}

inode_reference namei_parent(const char *pathname)
{
        if (strcmp(pathname, "/") == 0)
                return sblock_incore.root_dir;

        cofs_inode inode;
        inode_reference inum = sblock_incore.root_dir;
        if (!read_inode(&inode, inum))
                        return INODE_MISSING;

        ++pathname; // skip past the leading / character

        char *pathname_mut = strdup(pathname);
        char *strtok_arg = dirname(pathname_mut);
        const char *dirent;
        while ((dirent = strtok(strtok_arg, "/")) != NULL) {
                if (inode.type == INODE_TYPE_SYML) {
                        // don't think this should ever happen?
                } else if (inode.type != INODE_TYPE_DIR) {
                        cofs_errno = ENOTDIR;
                        inum = INODE_MISSING;
                        goto cleanup;
                }

                inum = Dir_lookup(&inode, dirent);

                if (inum == INODE_MISSING
                    || !read_inode(&inode, inum))
                {
                        inum = INODE_MISSING;
                        goto cleanup;
                }

                // RIP this for being so expensive LOL
                update_inode_atime(&inode);
                if (!write_inode(&inode, inode.inum))
                        goto cleanup;

                strtok_arg = NULL;
        }

cleanup:
        free(pathname_mut);
        return inum;
}

bool decrement_inode_refcount(cofs_inode *inode)
{
        bool ret = true;

        if (--inode->refcount == 0) {
                ret = release_datablocks(inode, 0);
                ret = free_inode(inode->inum) && ret;
        }

        return ret;
}

bool create_node(uint8_t type,
                 cofs_inode *parent,
                 const char *base_name,
                 inode_permissions mode,
                 uid_t uid, gid_t gid)
{
        bool ret = false;

        inode_reference me = allocate_inode();
        if (me > sblock_incore.ilist_size * INODES_PER_BLOCK)
                COFS_ERROR(ENOSPC); // no space left

        cofs_inode newnode = {
                .in_use = 1, .inum = me,
                .permissions = {.as_int = mode.as_int},
                .n_bytes = 0, .n_blocks = 0,
                .refcount = 1, .uid = uid, .gid = gid
        };

        update_inode_atime(&newnode);
        update_inode_mtime(&newnode);
        update_inode_ctime(&newnode);
        update_inode_btime(&newnode);

        if ((type == INODE_TYPE_DIR && !Dir_create(&newnode, parent))
          || (type == INODE_TYPE_SYML && /*TODO*/ true))
                goto cleanup;

        if (!Dir_addEntry(parent, base_name, me))
                goto cleanup;

        if (!write_inode(&newnode, me))
                goto cleanup;

        ret = true;

cleanup:
        if (!ret)
                free_inode(me);

        return ret;
}

mode_t get_st_mode(const cofs_inode *inode)
{
        mode_t result;
        result = inode->permissions.as_int;
        switch (inode->type) {
            case INODE_TYPE_DIR:
                result |= S_IFDIR;
                break;
            case INODE_TYPE_FILE:
                result |= S_IFREG;
                break;
            case INODE_TYPE_SYML:
                result |= S_IFLNK;
                break;
            case INODE_TYPE_SPEC:
                result |= S_IFBLK;
                break;
        }
        return result;
}

inode_permissions get_ino_perms(mode_t mode)
{
        return (inode_permissions){ .as_int = mode & ~(S_IFMT) };
}

bool check_read_permission(uid_t uid, gid_t gid, const cofs_inode *ino)
{
        inode_permissions perms = ino->permissions;
        return (uid == ino->uid && perms.owner_r)
                || (gid == ino->gid && perms.group_r)
                || (perms.world_r);
}

bool check_write_permission(uid_t uid, gid_t gid, const cofs_inode *ino)
{
        inode_permissions perms = ino->permissions;
        return (uid == ino->uid && perms.owner_w)
                || (gid == ino->gid && perms.group_w)
                || (perms.world_w);
}

bool check_exec_permission(uid_t uid, gid_t gid, const cofs_inode *ino)
{
        inode_permissions perms = ino->permissions;
        return (uid == ino->uid && perms.owner_x)
                || (gid == ino->gid && perms.group_x)
                || (perms.world_x);
}

bool update_inode_atime(cofs_inode *inode)
{
        return clock_gettime(CLOCK_REALTIME, &inode->atim) == 0;
}

// TODO: check which of these should also modify the access time
bool update_inode_mtime(cofs_inode *inode)
{
//        update_inode_atime(inode);
        return clock_gettime(CLOCK_REALTIME, &inode->mtim) == 0;
}

bool update_inode_ctime(cofs_inode *inode)
{
//        update_inode_atime(inode);
        return clock_gettime(CLOCK_REALTIME, &inode->ctim) == 0;
}

bool update_inode_btime(cofs_inode *inode)
{
        return clock_gettime(CLOCK_REALTIME, &inode->btim) == 0;
}