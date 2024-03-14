/*
 * layer2.h - Layer 2 interface functions that aren't syscalls
 */

#pragma once

#include "cofs_data_structures.h"

#define INODE_MISSING           (SIZE_MAX)

/**
 * Populates a statbuf from the given inode. Basically getattr() without the FUSE
 * @param statbuf
 * @param inode
 */
void fill_statbuf(struct stat *statbuf, const cofs_inode *inode);

/**
 * Converts a high-level (path) name to a low-level (inode) ID
 * @param pathname null-terminated pathname to the file
 * @return inode number of matching path, or else `INODE_MISSING`
 *      if the path does not exist
 */
inode_reference namei(const char *pathname);

/**
 * Like `namei()`, but gives the inode of the parent directory
 * @param pathname null-terminated pathname to the file (INCLUDING its basename)
 * @return inode number of parent directory, or else `INODE_MISSING`
 *      if the path does not exist
 */
inode_reference namei_parent(const char *pathname);

/**
 * Looks up the file handle ID given to us by FUSE to check if we
 *  have its inode cached in our memory
 * @param fhid fuse file handle ID
 * @return inode number if found in cache, else INODE_MISSING
 */
inode_reference lookup_file_handle(uint64_t fhid);

/**
 * Adds a entry to associate the file handle with the inode number
 * @param fhid file handle ID to store
 * @param ino inode corresponding to it
 */
void cache_file_handle(uint64_t fhid, inode_reference ino);

/**
 * Removes a file handle from the associative map
 * @param fhid file handle ID to erase
 */
void drop_file_handle(uint64_t fhid);

/**
 * Calls the gnu version of basename
 * See basename(3)
 * @param path
 * @return basename
 */
char *gnu_basename(const char *path);

/**
 * @param inode
 * @return A valid mode_t from the inode (suitable for e.g. st_mode)
 */
mode_t get_st_mode(const cofs_inode *inode);

/**
 * @param mode
 * @return A valid inode_permissions object constructed from the mode argument
 */
inode_permissions get_ino_perms(mode_t mode);

/**
 * Decrements the inode's reference count, deallocating its data upon reaching 0
 * @param inode inode to decrement the count of
 * @return `true` on success, else `false`
 */
bool decrement_inode_refcount(cofs_inode *inode);

/**
 * Creates a file or directory without performing any safety/permission checks
 * @param parent inode number of the parent
 * @param base_name the node's basename
 * @param mode file creation mode
 * @param uid   User ID of the caller
 * @param gid   Group ID of the caller
 * @return `true` on success, else `false`
 */
bool create_node(uint8_t type, cofs_inode *parent, const char *base_name, inode_permissions mode, uid_t uid, gid_t gid);

/**
 * Checks if the uid/gid combo have read permission
 * @param uid UID
 * @param gid GID
 * @param ino permissions
 * @return `true` if yes, else `false`
 */
bool check_read_permission(uid_t uid, gid_t gid, const cofs_inode *ino);

/**
 * Checks if the uid/gid combo have write permission
 * @param uid UID
 * @param gid GID
 * @param ino permissions
 * @return `true` if yes, else `false`
 */
bool check_write_permission(uid_t uid, gid_t gid, const cofs_inode *ino);

/**
 * Checks if the uid/gid combo have execute permission
 * @param uid UID
 * @param gid GID
 * @param ino permissions
 * @return `true` if yes, else `false`
 */
bool check_exec_permission(uid_t uid, gid_t gid, const cofs_inode *ino);

/**
 * Updates the inode's atim field with the current wall clock time
 * @param inode Inode to update
 */
bool update_inode_atime(cofs_inode *inode);

/**
 * Updates the inode's mtim field with the current wall clock time
 * @param inode Inode to update
 */
bool update_inode_mtime(cofs_inode *inode);

/**
 * Updates the inode's ctim field with the current wall clock time
 * @param inode Inode to update
 */
bool update_inode_ctime(cofs_inode *inode);

/**
 * Updates the inode's btim field with the current wall clock time
 * @param inode Inode to update
 */
bool update_inode_btime(cofs_inode *inode);