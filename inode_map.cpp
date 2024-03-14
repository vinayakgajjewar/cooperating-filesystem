/* inode_map.cpp - wrapper around std::map that can be called from C
 * implements lookup functionality to associate FUSE file handle ID's
 * with COFS inodes
 */

#include <unordered_map>
#include <cstdint>
#include <stdexcept>

using file_handle = uint64_t;

extern "C" {

#define _Static_assert(...)
#include "layer2.h"
#include "cofs_data_structures.h"
#undef _Static_assert

};

std::unordered_map<file_handle, inode_reference> id_map;

extern "C"
inode_reference lookup_file_handle(file_handle fhid)
{
        try {
                return id_map.at(fhid);
        } catch (std::out_of_range &e) {
                return INODE_MISSING;
        }
}

extern "C"
void cache_file_handle(file_handle fhid, inode_reference ino)
{
        id_map.insert({fhid, ino});
}

extern "C"
void drop_file_handle(file_handle fhid)
{
        id_map.erase(fhid);
}