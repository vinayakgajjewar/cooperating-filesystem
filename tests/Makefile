PARENTDIR = '..'

-include ../Rules.mk

CXXFLAGS += -I../

${PARENTDIR}/layer%.a:
	@echo "remaking layer$*.a"
	${MAKE} -C ${PARENTDIR} layer$*.a

cofs_mkfs.o: ../cofs_mkfs.c
	${CC} ${CFLAGS} -DCOFS_TEST_FREELIST $^ -o $@ -c

freelist.test: test_freelist.cpp cofs_mkfs.o ${PARENTDIR}/layer1.a
	${CXX} ${CXXFLAGS} ${LDFLAGS} $^ -o $@

datablocks.test: test_datablocks.cpp ${PARENTDIR}/layer2.a
	${CXX} ${CXXFLAGS} ${LDFLAGS} $^ -o $@ \
#	-Wl,-wrap,FreeList_create \
#	-Wl,-wrap,FreeList_init \
#	-Wl,-wrap,FreeList_append \
#	-Wl,-wrap,FreeList_pop \

writebig.test: writebig.cpp
	${CXX} ${CXXFLAGS} ${LDFLAGS} $^ -o $@

%.c.o:
	${CC} ${CFLAGS} $^ -c

clean:
	rm -f *.o *.test

.PHONY: clean ${PARENTDIR}/layer%.a