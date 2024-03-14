/* cofs_syscalls.h - layer3 top-level interface. contains all system calls into COFS
 * Really it's cleaner to just make this be layer 3 instead.
 */

#pragma once

#include <fuse.h>
#include <poll.h>

#include "cofs_data_structures.h"

/*
 * NOTE: the API return values/params don't have to perfectly conform to Linux syscalls,
 * since we are implementing our interface through FUSE, we just need a consistent way
 * to talk to our layer 3 code. For the most part, stuff is kept identical to the man
 * pages, but a couple exceptions (like readdir()) exist.
 */

int cofs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int cofs_readlink(const char *pathname, char *source, size_t maxlen);

int cofs_mkdir(const char *pathname, mode_t mode);
int cofs_mknod(const char *pathname, mode_t mode, dev_t dev);
int cofs_unlink(const char *pathname);
int cofs_rmdir(const char *pathname);

int cofs_symlink(const char *linkname, const char *source);
int cofs_rename(const char *name1, const char *name2, unsigned int flags);
int cofs_chmod(const char *pathname, mode_t mode, struct fuse_file_info *fi);
int cofs_chown(const char *pathname, uid_t uid, gid_t gid, struct fuse_file_info *fi);
int cofs_truncate (const char *pathname, off_t size, struct fuse_file_info *fi);

int cofs_open(const char *pathname, struct fuse_file_info *fi);
int cofs_read(const char *path, char *buf, size_t count, off_t offset, struct fuse_file_info *fi);
int cofs_write(const char *path, const char *buf, size_t count, off_t offset, struct fuse_file_info *fi);
int cofs_statfs(const char *, struct statvfs *statbuf);

int cofs_opendir(const char *pathname, struct fuse_file_info *fi);
int cofs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags);

int cofs_utimens(const char *pathname, const struct timespec tv[2], struct fuse_file_info *fi);

static int cofs_poll(const char *path, struct fuse_file_info *fi, struct fuse_pollhandle *ph, unsigned *reventsp);

//int cofs_open(inode_reference inum, int flags, mode_t mode);

/* note: FUSE has no callback for close(). not sure what we'd need it to do anyway? */

// TODO: figure out how to impl read & write since we don't have access to file descriptors.
//  FUSE just passes us pathnames to the files, but then I'm unsure how to handle cases where
//  e.g. the file isn't already open? Does the FUSE kmodule handle that for us?
