/* cofs_directories.h - directory-related functions for COFS
 *
 *
 */

#pragma once

#include "cofs_data_structures.h"

/**
 * Populates a newly allocated inode as a directory and creates the `.` and `..` entries
 * @param dir
 * @return
 */
bool Dir_create(cofs_inode *dir, cofs_inode *parent);

/**
 * Adds a new entry to a directory
 * @param dir directory to add the entry to
 * @param name name of the new entry
 * @param inum inode number of the new entry
 * @return `true` on success, else `false`
 */
bool Dir_addEntry(cofs_inode *dir, const char *name, inode_reference inum);

/**
 * Finds the inode belonging to a name in directory dir
 * @param dir the directory to search
 * @param name the pathname
 * @return inode reference, or INODE_MISSING if not found
 */
inode_reference Dir_lookup(cofs_inode *dir, const char *name);

/**
 * Removes an entry from a directory, decrementing its refcount and freeink if necessary
 * @param dir the directory to operate on
 * @param name name to be unlinked
 * @return `true` on success, else `false`
 */
bool Dir_removeEntry(cofs_inode *dir, const char *name);