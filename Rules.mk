DEBUG ?= 1

CC	 = gcc
CFLAGS   = -std=gnu17 -Wall -Wextra -Wno-unused #-fsanitize=address

CXX	 = g++
CXXFLAGS = -std=gnu++20 -Wall -Wextra -Wno-unused #-fsanitize=address

AR	 = ar
ARFLAGS  = -rcs

LIBS	 = fuse3
CFLAGS  += $(foreach lib,${LIBS},$(shell pkg-config --cflags ${lib}))
CFLAGS  += -DFUSE_USE_VERSION=31
LDFLAGS += $(foreach lib,${LIBS},$(shell pkg-config --libs ${lib}))
EXE_NAME = cofs

LAYER0   = layer0.o cofs_errno.o

LAYER1	 = ${LAYER0} free_list.o superblock.o cofs_inode_functions.o

LAYER2	 = ${LAYER1} cofs_mkfs.o layer2.o cofs_datablocks.o cofs_files.o gnu_basename.o cofs_directories.o

LAYER3	 = ${LAYER2} cofs_syscalls.o

OBJECTS  = cofs_main.o ${LAYER3}

ifneq ($(strip ${DEBUG}), 1)
	CFLAGS	 += -O3 -flto
	CXXFLAGS += -O3 -flto
	LDFLAGS  += -flto
else
	CFLAGS   += -ggdb3 -Og -DDEBUG
	CXXFLAGS += -ggdb3 -Og -DDEBUG
endif