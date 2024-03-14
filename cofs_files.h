/* cofs_files.h - file-related functions for COFS
 *
 */

#pragma once

#include "cofs_data_structures.h"

/**
 * reads data from file starting at start up until start + length into buf
 * @param file file inode to read from
 * @param buf buffer to put data into
 * @param start initial offstet in file to start reading from
 * @param length amount of data to read
 * @return `true` if success, else `false`
 */
bool File_readData(cofs_inode *file, char *buf, off_t start, size_t length);

/**
 * writes data into file starting at start up until start + length into buf
 * @param file file inode to write to
 * @param buf buffer to write data out of
 * @param start starting offset in file (in bytes)
 * @param length amount of data to write to file
 * @return `true` if success, else `false`
 */
bool File_writeData(cofs_inode *file, const char *buf, off_t start, size_t length);

/**
 * Truncates a file to specified legnth
 * @param file File to truncate
 * @param newsize size to grow/shrink to
 * @return `true` on success, else `false`
 */
bool File_truncate(cofs_inode *file, size_t newsize);