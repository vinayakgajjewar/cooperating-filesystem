/* util.h - common utility stuff for COFS
 *
 */

#pragma once

#ifdef DEBUG
#include <stdio.h>
#define PRINT_ERR(fmt, args...)     \
        fprintf(stderr, fmt, ##args)
#else
#define PRINT_ERR(...)
#endif

#ifdef DEBUG
#define PRINT_DBG(fmt, args...)     \
        printf(fmt, ##args)
#else
#define PRINT_DBG(...)
#endif

/**
 * performs a calloc() call and checks for failure/prints error message
 */
#define CALLOC_CHECK(var, count, size) \
        do {                                                                  \
                var = calloc(count, size);                                    \
                if (var == NULL) {                                            \
                        PRINT_ERR("%s = calloc(%zu, %zu) failed at %s:%d\n",  \
                                  #var, (size_t) count, (size_t) size,        \
                                  __FILE__, __LINE__);                        \
                }                                                             \
        } while(0)

/**
 * performs a malloc() call and checks for failure/prints error message
 */
#define MALLOC_CHECK(var, size) \
        do {                                                                  \
                var = malloc(size);                                           \
                if (var == NULL) {                                            \
                        PRINT_ERR("%s = malloc(%zu) failed at %s:%d\n",       \
                                  #var, (size_t) size,                        \
                                  __FILE__, __LINE__);                        \
                }                                                             \
        } while(0)

#define MALIGN_CHECK(var, size) \
        do {                                                                  \
                var = aligned_alloc(size, size);                              \
                if (var == NULL) {                                            \
                        PRINT_ERR("%s = malloc(%zu) failed at %s:%d\n",       \
                                  #var, (size_t) size,                        \
                                  __FILE__, __LINE__);                        \
                }                                                             \
        } while(0)
