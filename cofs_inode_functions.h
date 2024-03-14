/* cofs_inode_functions.h - COFS functions for allocating, freeing, reading, and writing inodes
* 
*/

#pragma once
#include "cofs_data_structures.h"

/**
 * Creates the ilist during mkfs routine
 * @param ilist_size Number of inodes in the ilist
 * @return `true` on success, else `false`
 */
bool ilist_create(size_t ilist_size);

/**
 * Allocates an inode for a new file
 * @return Index of allocated inode in ilist on success, else -1
 */
inode_reference allocate_inode();

/**
 * Frees an existing inode
 * @param index index of inode in ilist
 * @return 'true' if success, else 'false' 
 */
bool free_inode(inode_reference index);

/**
 * Reads an inode from disk
 * @param inode a pointer to read the cofs_inode into
 * @param index index of inode in ilist
 * @return 'true' if success, else 'false' 
 */
bool read_inode(cofs_inode* inode, inode_reference index);

/**
 * Writes an inode to disk
 * @param inode a pointer to a cofs_inode that needs to be written to disk
 * @param index index to write inode to in the ilist
 * @return 'true' if success, else 'false' 
 */
bool write_inode(cofs_inode* inode, inode_reference index);