all: libkoishi.a

libkoishi.a: libkoishi.o
	ar r libkoishi.a libkoishi.o

libkoishi.o: libkoishi.c libkoishi.h
	gcc -c -o libkoishi.o -Wall libkoishi.c

.PHONY: all