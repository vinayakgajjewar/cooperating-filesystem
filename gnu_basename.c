/*
 * Created by ryan on 11/30/23.
 * Can't believe I have to do this.
 */

#define _GNU_SOURCE
#include <string.h>

#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
extern char *gnu_basename(const char *path)
{ return basename(path); }