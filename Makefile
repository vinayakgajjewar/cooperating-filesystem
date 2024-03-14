# COFS - Cooperating Filesystem
# A UNIX-like filesystem built with FUSE
# UCSB CS 270 Fall 2023

-include Rules.mk

target: ${EXE_NAME} mkfs.cofs

all: target mkfs.cofs test

test: freelist.test datablocks.test writebig.test

${EXE_NAME}: ${OBJECTS}
	${CC} ${LDFLAGS} $^ -o $@

layer0.a: ${LAYER0}
	${AR} ${ARFLAGS} $@ $^

layer1.a: ${LAYER1}
	${AR} ${ARFLAGS} $@ $^

layer2.a: ${LAYER2}
	${AR} ${ARFLAGS} $@ $^

mkfs.cofs: cofs_mkfs.c ${LAYER1}
	${CC} ${CFLAGS} -DBUILD_MKFS_PROGRAM ${LDFLAGS} $^ -o $@

fsck.cofs: ${LAYER1} cofs_fsck.o cofs_inode_functions.o
	${CC} ${CFLAGS} ${LDFLAGS} $^ -o $@

%.c.o:
	${CC} ${CFLAGS} $^ -c

%.cpp.o:
	${CXX} ${CXXFLAGS} $^ -c

%.test:
	${MAKE} -C tests $@
	@echo "[[Running test '$*']]"
	@./tests/$@

clean:
	rm -f *.o layer*.a ${EXE_NAME} mkfs.cofs fsck.cofs
	${MAKE} -C tests clean

.PHONY: clean all test cofs *.test
