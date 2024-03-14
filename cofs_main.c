/* cofs_main.c - Entry point to COFS filesystem FUSE driver.
 *
 * UCSB CS 270. Fall 2023
 * Cooperating System
 *
 * To build: make cofs
 * To run: ./cofs [OPTIONS] <mount-point>
 *      - see ./cofs --help for detailed options
 * To debug: ./cofs -f -d --use-mem <size> <mount-point>
 * To unmount fusermount3 -u <mount-point>
 *
 * Code derived from libfuse/examples/hello.c
 *      - Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
*/

#include <fuse.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdbool.h>

#include "layer0.h"
#include "cofs_util.h"
#include "cofs_syscalls.h"
#include "superblock.h"
#include "free_list.h"
#include "cofs_errno.h"

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	int show_help;
        const char *mem_size;
        const char *blkdev;
} options;

#define OPTION_FLAG(opt, var)           \
        { opt, offsetof(struct options, var), 1 }

#define OPTION_PARAM(opt, arg, var) \
        { opt" "arg, offsetof(struct options, var), 0 }

static const struct fuse_opt option_spec[] = {
        OPTION_FLAG("-h", show_help),
        OPTION_FLAG("--help", show_help),
        OPTION_PARAM("-m", "%s", mem_size),
        OPTION_PARAM("--use-mem", "%s", mem_size),
        OPTION_PARAM("-b", "%s", blkdev),
        OPTION_PARAM("--blkdev", "%s", blkdev),
        FUSE_OPT_END
};

static int hello_open(const char *path, struct fuse_file_info *fi)
{
        (void) path;
        if ((fi->flags & O_ACCMODE) != O_WRONLY)
        {
                return -EACCES;
        }

	return 0;
}

static int hello_write(const char *path, const char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
        (void) fi; (void) offset;

        return (1) ? size : 0;
}

static void show_help(const char *progname)
{
	printf("usage: %s -m <size> <mountpoint>\n"
               "       %s -b <block_device> <mountpoint>\n\n",
               progname, progname);

        printf("COFS filesystem-specific options:\n"
               "    -m, --use-mem=<size>        Create and use an in-memory filesystem\n"
               "                                (<size> may include a suffix B|K|M|G)\n"
               "    -b, --blkdev=<device>       Block device containing the filesystem\n"
               "The `-m' and `-b' options are mutually exclusive.\n"
               "\n");
}
static size_t parse_mem_size(const char *str)
{
        // should return 0 on format error

        if (!str)
                return 0;

        size_t sz = 0;
        char suffix = 'B';

        if (sscanf(str, "%zu%c", &sz, &suffix) == 0) { //NOLINT cert-err34-c
                PRINT_ERR("Invalid size: '%s'\n", str);
                return 0;
        }

#pragma GCC diagnostic ignored "-Wimplicit-fallthrough="
        switch (suffix) {
            case 'G': //NOLINT bugprone-branch-clone
                sz <<= 10;
            case 'M':
                sz <<= 10;
            case 'K':
                sz <<= 10;
            case 'B':
                break;
            default:
                PRINT_ERR("Invalid size suffix: '%c'\n", suffix);
                break;
        }

        return sz;
}

static struct {
        size_t memsize; // memsize in bytes
        const char *blkdev; // block device backing
        // anything else?
} cofs_init_args;

// return false on error and true on success
static bool setup(struct options *opts)
{
        // options are mutually exclusive
        if ((opts->blkdev == NULL) == (opts->mem_size == NULL))
                return false;

        cofs_init_args.memsize = parse_mem_size(opts->mem_size);
        // check that we can access the device file if necessary
        if (opts->blkdev && access(opts->blkdev, F_OK) == -1) {
                fprintf(stderr, "No such file: '%s'\n\n", opts->blkdev);
                return false;
        } else {
                cofs_init_args.blkdev = opts->blkdev;
        }

        return true;
}

_Noreturn static void cofs_abort()
{
        struct fuse *fs = fuse_get_context()->fuse;
        fuse_exit(fs);
        fuse_unmount(fs);
        fuse_destroy(fs);
        exit(EXIT_FAILURE);
}

static void *cofs_init(struct fuse_conn_info *inf, struct fuse_config *conf)
{
        CLEAR_ERRNO();
        // see /usr/include/fuse3/fuse.h#L96
        conf->intr = 0;
        conf->use_ino = 1;
        conf->kernel_cache = 0;
        conf->direct_io = 0;
        conf->nullpath_ok = 1;
//        conf->parallel_direct_writes = 0;

        // see /usr/include/fuse3/fuse_common.h#L460
//        inf->proto_major = 0;
//        inf->proto_minor = 1;
        // maximum write buffer size
        // inf->max_write = ?;
        // max read buffer size
        // inf->max_read = ?;
        unsigned int capabilities = inf->capable;
        capabilities |= (FUSE_CAP_READDIRPLUS | FUSE_CAP_CACHE_SYMLINKS);
//        capabilities &= ~(FUSE_CAP_ASYNC_READ | FUSE_CAP_ASYNC_DIO | FUSE_CAP_PARALLEL_DIROPS);
        inf->want = capabilities;

        if (!layer0_init(cofs_init_args.blkdev, cofs_init_args.memsize)) {
                PRINT_ERR("Layer 0 initialization failed\n");
                cofs_abort();
        }

        if (!FreeList_init(sblock_incore.flist_head)) {
                PRINT_ERR("FreeList initialization failed\n");
                cofs_abort();
        }

        return NULL;
}

static void cofs_destroy(void *private_data)
{
        layer0_teardown();
}

static int cofs_fsync()
{ return 0; }

static int cofs_lock()
{ return 0; }

static int cofs_fsyncdir()
{ return 0; }

static off_t cofs_lseek(const char *, off_t off, int whence, struct fuse_file_info *)
{
        return off;
}

static const struct fuse_operations cofs_oper = {
	.init           = cofs_init,
        .destroy        = cofs_destroy,

	.getattr	= cofs_getattr,
        .readlink       = cofs_readlink, /* TODO */

        .mkdir          = cofs_mkdir,
        .mknod          = cofs_mknod,
        .unlink         = cofs_unlink,
        .rmdir          = cofs_rmdir,

        .symlink        = cofs_symlink, /* paritally works */
        .rename         = cofs_rename,
        .link           = NULL,
        .chmod          = cofs_chmod,
        .chown          = cofs_chown,
        .truncate       = cofs_truncate,

        .open		= cofs_open,
        .read           = cofs_read,
        .write          = cofs_write,
        .statfs         = cofs_statfs,
        .release        = NULL, // don't need
//        .fsync          = cofs_fsync,

        .opendir        = cofs_opendir,
        .readdir	= cofs_readdir,
//        .fsyncdir       = cofs_fsyncdir,
        .releasedir     = NULL, // don't need

//        .lock           = cofs_lock,
        .utimens        = cofs_utimens,

        .poll           = cofs_poll,
        .lseek          = NULL,
};

static const char* our_fuse_options[] = {
        "-s",
        "-o",
        (
                // have kernel/fuse perform permission checking for us (THANK YOU)
                "default_permissions,"
                // allow all users to access the filesystem
                // note: requires adding 'user_allow_other' to /etc/fuse.conf
                // unless the program is invoked by root. see mount.fuse(8)#CONFIGURATION
                "allow_other"
        ),

        NULL // end options list
};

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

        /* add our required args to the option vector */
        for (const char **arg = our_fuse_options; *arg != NULL; ++arg)
                fuse_opt_add_arg(&args, *arg);

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	} else if (!setup(&options)) {
                show_help(argv[0]);
                printf("For general FUSE options, try running %s --help.\n", argv[0]);
                ret = EXIT_FAILURE;
                goto lbl_cleanup;
        }

	ret = fuse_main(args.argc, args.argv, &cofs_oper, NULL);

lbl_cleanup:
        layer0_teardown();
	fuse_opt_free_args(&args);
	return ret;
}
