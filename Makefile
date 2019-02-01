CFLAGS = -g -O2 -Wall
LDFLAGS = -pthread

pgc: heap.o rbtree.o resident-keeper.o sigbus-fixup.o \
	transient-pager.o util.o victim-checker.o
