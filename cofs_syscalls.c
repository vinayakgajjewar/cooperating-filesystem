/* cofs_syscalls.c - layer3 top-level interface. contains all system calls into COFS
 *
 */

#include "cofs_syscalls.h"

#include <fuse.h>
#include <errno.h>
#include <libgen.h>
#include <linux/fs.h>
#include <sys/statvfs.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "layer2.h"
#include "cofs_directories.h"
#include "cofs_files.h"
#include "cofs_inode_functions.h"
#include "cofs_errno.h"
#include "layer0.h"
#include "cofs_datablocks.h"
#include "superblock.h"
#include "cofs_util.h"

static cofs_inode my_ino;

// finds the target inode by looking first in the inode cache, then by calling namei()
static inode_reference find_target(struct fuse_file_info *fi, const char *path)
{
        if (fi)
                return fi->fh;

        return namei(path);
}

int cofs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, path);
        if (target == INODE_MISSING)
                return -cofs_errno;

        if (!read_inode(&my_ino, target))
                return -cofs_errno;

        fill_statbuf(stbuf, &my_ino);

        return -cofs_errno;
}

int cofs_readlink(const char *pathname, char *source, size_t maxlen)
{
        CLEAR_ERRNO();

        inode_reference target = namei(pathname);

        if (target == INODE_MISSING)
                return -ENOENT;

        if (!read_inode(&my_ino, target))
                return -cofs_errno;

        if (my_ino.type != INODE_TYPE_SYML)
                return -EINVAL;

        // note: does truncation still involve writing the null byte?
        size_t pathlen = my_ino.n_bytes; // inode doesn't store \0 terminator
        size_t to_read = pathlen < (maxlen-1) ? pathlen : (maxlen-1);
        for (size_t i = 0; i < SYML_SOURCE_PATHLEN; i++) {
                if (i == to_read) {
                        source[to_read] = '\0';
                        break;
                }

                source[i] = my_ino.syml.source_path[i];
        }
        if (to_read > SYML_SOURCE_PATHLEN) {
                // TODO: need to read data from direct blocks
        }

        return -cofs_errno;
}

int cofs_mkdir(const char *pathname, mode_t mode)
{
        CLEAR_ERRNO();

        struct fuse_context *context = fuse_get_context();

        inode_permissions mode_with_umask = get_ino_perms(mode & ~(context->umask));


        inode_reference parent = namei_parent(pathname);
        if (parent == INODE_MISSING) {
                if (cofs_errno == 0)
                        cofs_errno = ENOENT;
                goto cleanup;
        }

        if (!read_inode(&my_ino, parent))
                goto cleanup;

        if (!create_node(INODE_TYPE_DIR, &my_ino,
                         gnu_basename(pathname), mode_with_umask,
                         context->uid, context->gid))
                cofs_errno = ESOCKTNOSUPPORT; // change this LOL

cleanup:
        return -cofs_errno; // STUB
}

int cofs_mknod(const char *pathname, mode_t mode, dev_t dev)
{
        CLEAR_ERRNO();
        // NOTE TO SELF: remember to write back the parent directoru inode to disk
        // after calling Dir_removeEntry()
        struct fuse_context *context = fuse_get_context();

        inode_permissions mode_with_umask = get_ino_perms(mode & ~(context->umask));

        inode_reference parent = namei_parent(pathname);
        if (parent == INODE_MISSING) {
                if (cofs_errno == 0)
                        cofs_errno = ENOENT;
                goto cleanup;
        }

        if (!read_inode(&my_ino, parent))
                goto cleanup;

        if (!create_node(INODE_TYPE_FILE, &my_ino,
                         gnu_basename(pathname), mode_with_umask,
                         context->uid, context->gid))
                cofs_errno = ESOCKTNOSUPPORT; // change this LOL

cleanup:
        return -cofs_errno;
}

int cofs_unlink(const char *pathname)
{
        CLEAR_ERRNO();
        inode_reference parent_dir = namei_parent(pathname);
        if (parent_dir == INODE_MISSING)
                return -cofs_errno;

        const char *base_name = gnu_basename(pathname);
        cofs_inode par_ino;
        if (!read_inode(&par_ino, parent_dir))
                return -cofs_errno;

        inode_reference target = Dir_lookup(&par_ino, base_name);
        if (target == INODE_MISSING)
                return -cofs_errno;

        cofs_inode ino;
        if (!read_inode(&ino, target))
                return -cofs_errno;

        if (ino.type == INODE_TYPE_DIR)
                return -EISDIR;

        Dir_removeEntry(&par_ino, base_name);

        return -cofs_errno;
}

int cofs_rmdir(const char *pathname)
{
        CLEAR_ERRNO();

        CLEAR_ERRNO();
        inode_reference parent_dir = namei_parent(pathname);
        if (parent_dir == INODE_MISSING)
                return -cofs_errno;

        const char *base_name = gnu_basename(pathname);
        cofs_inode par_ino;
        if (!read_inode(&par_ino, parent_dir))
                return -cofs_errno;

        inode_reference target = Dir_lookup(&par_ino, base_name);
        if (target == INODE_MISSING)
                return -cofs_errno;

        cofs_inode ino;
        if (!read_inode(&ino, target))
                return -cofs_errno;

        if (ino.type != INODE_TYPE_DIR)
                return -ENOTDIR;

        if (ino.num_direntries > 2) // don't remove if we hold anything more than '.' and '..'
                return -ENOTEMPTY;

        Dir_removeEntry(&par_ino, base_name);

        return -cofs_errno;
}

int cofs_symlink(const char *linkname, const char *source)
{
        CLEAR_ERRNO();

        /* STUB so i can test readlink */

        char TEST_SYMLINK_PATH[] = {'/', 'b', 'i', 'n', '/', 'b', 'a', 's', 'h'};
        _Static_assert(sizeof TEST_SYMLINK_PATH <= SYML_SOURCE_PATHLEN);

        cofs_inode symlink = {
                .type = INODE_TYPE_SYML,
                .permissions = {.as_int = 0777}, // rwxrwxrwx
                .in_use = 1,
                .n_bytes = sizeof(TEST_SYMLINK_PATH),
                .uid = 1000, .gid = 1000,
        };

        memcpy(symlink.syml.source_path, TEST_SYMLINK_PATH, sizeof TEST_SYMLINK_PATH);

        read_inode(&my_ino, sblock_incore.root_dir);
        inode_reference s = allocate_inode();
        symlink.inum = s;
        write_inode(&symlink, s);

        Dir_addEntry(&my_ino, gnu_basename(linkname), s);

        return -cofs_errno;
}

int cofs_rename(const char *name1, const char *name2, unsigned int flags)
{
        CLEAR_ERRNO();

        // TODO

        return -cofs_errno;
}

int cofs_chmod(const char *pathname, mode_t mode, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, pathname);
        if (target == INODE_MISSING)
                return -cofs_errno;

        if (!read_inode(&my_ino, target))
                return -cofs_errno;

        my_ino.permissions = get_ino_perms(mode);
        update_inode_ctime(&my_ino);

        write_inode(&my_ino, target);
        return -cofs_errno;
}

int cofs_chown(const char *pathname, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, pathname);
        if (target == INODE_MISSING)
                return -cofs_errno;

        if (!read_inode(&my_ino, target))
                return -cofs_errno;

        my_ino.uid = uid;
        my_ino.gid = gid;
        my_ino.permissions.sg = 0;
        my_ino.permissions.su = 0;

        update_inode_ctime(&my_ino);
        write_inode(&my_ino, target);
        return -cofs_errno;
}

int cofs_truncate(const char *pathname, off_t size, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, pathname);
        if (target == INODE_MISSING)
                return -cofs_errno;

        // TODO

        return -cofs_errno;
}

int cofs_open(const char *pathname, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = namei(pathname);
        if (target == INODE_MISSING)
                return -cofs_errno;

        fi->fh = target;

        // TODO: update inode access time?
        struct fuse_context *context = fuse_get_context();
//        context->umask

        return -cofs_errno;
}

int cofs_read(const char *path, char *buf, size_t count,
              off_t offset, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, path);

        if (target == INODE_MISSING)
                return -cofs_errno;

        cofs_inode *file = malloc(sizeof(cofs_inode));
        if (file == NULL)
                return -errno; // whatever error code malloc gave us

        if (!read_inode(file, target))
                goto cleanup;

        if (file->type == INODE_TYPE_DIR) {
                cofs_errno = EISDIR;
                goto cleanup;
        }

#pragma GCC diagnostic ignored "-Wsign-compare"
        if (file->n_bytes <= offset)
                goto cleanup; // return 0

        if (!File_readData(file, buf, offset, count))
                goto cleanup;

        update_inode_atime(file);
        write_inode(file, target);

cleanup:
        free(file);
        return (cofs_errno == 0) ? (int) count : -cofs_errno;
}

int cofs_write(const char *path, const char *buf, size_t count,
               off_t offset, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        if (offset + count > COFS_MAX_FILESIZE) {
                return -EFBIG;
        }

        inode_reference target = find_target(fi, path);
        if (target == INODE_MISSING)
                return -cofs_errno;

        cofs_inode *file = malloc(sizeof(cofs_inode));
        if (file == NULL)
                return -errno; // whatever error code malloc gave us

        if (!read_inode(file, target))
                goto cleanup;

        if (file->type == INODE_TYPE_DIR) {
                cofs_errno = EISDIR;
                goto cleanup;
        }

        if (!File_writeData(file, buf, offset, count))
                goto cleanup;

        update_inode_mtime(file);
        write_inode(file, target);

cleanup:
        free(file);
        return (cofs_errno == 0) ? (int) count : -cofs_errno;
}

int cofs_statfs(const char *ignored, struct statvfs *statbuf)
{
        CLEAR_ERRNO();
        (void) ignored;

        statbuf->f_bsize = statbuf->f_frsize = COFS_BLOCK_SIZE;
        statbuf->f_blocks = sblock_incore.n_blocks - sblock_incore.ilist_size - 1;
        statbuf->f_bfree = statbuf->f_bavail = sblock_incore.free_blocks;
        statbuf->f_files = sblock_incore.ilist_size * INODES_PER_BLOCK;
        statbuf->f_ffree = statbuf->f_favail = sblock_incore.free_inodes;
        statbuf->f_namemax = MAX_FILE_BASENAME;
        statbuf->f_flag = 0x10; // ST_SYNCHRONOUS

        return -cofs_errno;
}

static int cofs_poll(const char *path, struct fuse_file_info *fi, struct fuse_pollhandle *ph, unsigned *reventsp) {

    // Get an inode reference.
    inode_reference my_inode = fi->fh;

    *reventsp = 0;

    // Not sure if it's as easy as this.
    *reventsp |= POLLIN;
    *reventsp |= POLLOUT;

    return 0;
}

/* I don't like placing non-toplevel syscalls here, but since this helper needs fuse
 * I'm granting an exception
 */
struct __readDir_Args {
    void *buf;
    fuse_fill_dir_t filler;
    enum fuse_fill_dir_flags flags;
    cofs_direntry *block_cache;
    struct stat *stbuf;
    size_t entries_to_read;
};

static bool __readDir_Iterator(block_reference block, void *_args)
{
        struct __readDir_Args *args = _args;
        if (layer0_readBlock(block, args->block_cache) != 0)
                return false;

        for (size_t i = 0; i < DIRENTRIES_PER_BLOCK; i++) {
                if (args->entries_to_read == 0)
                        return false; // END OF DIRECTORY

                cofs_direntry *entry = &args->block_cache[i];
                if (entry->inum == INODE_MISSING)
                        continue; // HOLE

                if (!read_inode(&my_ino, entry->inum))
                        return false;

                fill_statbuf(args->stbuf, &my_ino);

                --args->entries_to_read;

                if (args->filler(args->buf, entry->base_name, args->stbuf, 0, args->flags) == 1) {
                        COFS_ERROR(EAGAIN); // TODO: not really sure what this error should be
                }
        }

        return true;
}

int cofs_opendir(const char *pathname, struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = namei(pathname);
        if (target == INODE_MISSING)
                return -cofs_errno;

        fi->fh = target;

        // TODO: update inode access time?

        return -cofs_errno;
}

int cofs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
        (void) offset;
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, path);
        if (target == INODE_MISSING)
                return (cofs_errno == 0) ? -ENOENT : -cofs_errno;

        // can't use my_ino since the iterator uses it
        cofs_inode *dir = malloc(sizeof(cofs_inode));
        if (dir == NULL)
                return -errno; // whatever error code malloc gave us

        /* TODO: handle errors involving inode lookup here */

        if (!read_inode(dir, target))
                return -cofs_errno;

        if (dir->type != INODE_TYPE_DIR)
                return -ENOTDIR;

        update_inode_atime(dir);

        if (!write_inode(dir, target))
                return -cofs_errno;

        struct stat stbuf;

        struct __readDir_Args args = {
                .buf = buf,
                .filler = filler,
                .flags = (flags == FUSE_READDIR_PLUS ? FUSE_FILL_DIR_PLUS : 0),
                .block_cache = calloc(DIRENTRIES_PER_BLOCK, sizeof(cofs_direntry)),
                .stbuf = &stbuf, .entries_to_read = dir->num_direntries
        };

        if (args.block_cache == NULL) {
                free(dir);
                return -errno;
        }

        foreach_datablock_in_inode(dir, __readDir_Iterator, 0, true, &args);

        free(dir);
        free(args.block_cache);

        return -cofs_errno;
}

int cofs_utimens(const char *pathname, const struct timespec tv[2], struct fuse_file_info *fi)
{
        CLEAR_ERRNO();

        inode_reference target = find_target(fi, pathname);
        if (target == INODE_MISSING)
                return -cofs_errno;

        if (!read_inode(&my_ino, target))
                return -cofs_errno;

        if (tv == NULL) {
                update_inode_atime(&my_ino);
                update_inode_mtime(&my_ino);
                return -cofs_errno;
        }

        if (tv[0].tv_nsec == UTIME_NOW) {
                update_inode_atime(&my_ino);
        } else {
                my_ino.atim = tv[0];
        }

        if (tv[1].tv_nsec == UTIME_NOW) {
                update_inode_mtime(&my_ino);
        } else {
                my_ino.mtim = tv[1];
        }

        write_inode(&my_ino, target);

        return -cofs_errno;
}
