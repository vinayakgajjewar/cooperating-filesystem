/* cofs_errno.h -- errno stuff for COFS
 *
 */

#pragma once

#include <errno.h>

extern int cofs_errno;

#define COFS_ERROR(errno_val) \
        do {                  \
                cofs_errno = errno_val;       \
                return false;                 \
        } while (0)

// call thi at the start of every system call to ensure
//  we don't propagate errors across calls
#define CLEAR_ERRNO()           cofs_errno = 0