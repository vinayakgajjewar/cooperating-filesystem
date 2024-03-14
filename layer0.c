/* layer0.c - COFS layer 0 implementation
 *
 */

#include "layer0.h"

#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "cofs_parameters.h"
#include "cofs_util.h"
#include "cofs_mkfs.h"
#include "cofs_data_structures.h"
#include "superblock.h"
#include "cofs_errno.h"

static volatile unsigned char *backing;

static size_t unmap_size = 0;

size_t NUM_BLOCKS = 0;

// initialize layer 0 using memory
static void * __init_use_mem(size_t memsize)
{
        void *buf = mmap(NULL, memsize, PROT_WRITE | PROT_READ,
                          MAP_ANON | MAP_PRIVATE, 0, 0);

        if (buf == MAP_FAILED) {
                PRINT_ERR("Cannot allocate in-memory filesystem: %s\n",
                          strerror(errno));
                return NULL;
        }

//        void *buf = aligned_alloc(COFS_BLOCK_SIZE, memsize);

        unmap_size = memsize;
        backing = buf;

        if (!mkfs(memsize)) {
                PRINT_ERR("mkfs.cofs failed for in-memory filesystem\n");
                return NULL;
        }

        return buf;
}

// initialize layer 0 using the path to a block device
// We can change this later... but it feels cleaner to me than using lseek/read/write
void *layer0_mapBlkdev(const char *path, size_t *size)
{
        int fdd = open(path, O_RDWR | O_NONBLOCK);
        if (fdd < 0) {
                PRINT_ERR("Cannot open block device: %s\n",
                          strerror(errno));
                return NULL;
        }

        uint64_t blkdev_size = 0;
        ioctl(fdd, BLKGETSIZE64, &blkdev_size);

        PRINT_DBG("Loading block device %s with size %zu\n", path, *size);

        void *buf = mmap(NULL, blkdev_size, PROT_READ | PROT_WRITE,
                MAP_FILE | MAP_SHARED, fdd, 0);

        close(fdd);

        if (buf == MAP_FAILED) {
                PRINT_ERR("Cannot map block device: %s\n",
                          strerror(errno));
                return NULL;
        }

        // touch all of the pages so that we can access them later
//        for (size_t offset = 0; offset < blkdev_size; offset += sysconf(_SC_PAGESIZE)) {
//                volatile unsigned char a = *(volatile unsigned char *) (backing + offset);
//        }

        unmap_size = blkdev_size;
        *size = blkdev_size;

        backing = buf;

        return buf;
}

bool layer0_init(const char *blkdev, size_t memsize)
{
        backing = blkdev ? layer0_mapBlkdev(blkdev, &memsize)
                         : __init_use_mem(memsize);

        NUM_BLOCKS = unmap_size / COFS_BLOCK_SIZE;

        if (backing == NULL)
                return false;

        return layer0_readBlock(0, &sblock_incore) == 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored  "-Wdiscarded-qualifiers"
bool layer0_teardown(void)
{
        if (unmap_size) {
                // mmap(2) man page indicates that changes may not be committed before munmap()
                // call unless we cal msync()
                msync(backing, unmap_size, MS_SYNC);
                munmap(backing, unmap_size);
        }

        return true;
}

int layer0_writeBlock(block_reference bnum, const void *buf) {
    if (bnum >= NUM_BLOCKS) {
        cofs_errno = EIO;
        return -1;
    }

    unsigned char *dest = backing + (bnum * COFS_BLOCK_SIZE);

    memcpy(dest, buf, COFS_BLOCK_SIZE);
    msync(dest, COFS_BLOCK_SIZE, MS_ASYNC);

    return 0;
}

int layer0_readBlock(block_reference bnum, void *buf) {
    if (bnum >= NUM_BLOCKS) {
        cofs_errno = EIO;
        return -1;
    }

    memcpy(buf, backing + (bnum * COFS_BLOCK_SIZE), COFS_BLOCK_SIZE);
    return 0;
}
#pragma GCC diagnostic pop

// FOR TESTING ONLY!!
size_t getsize(void)
{
        return unmap_size;
}

#ifdef DEBUG

volatile unsigned char *getmembase(void)
{ return backing; }

#endif
