all: koishi

koishi: koishi.o libkoishi/libkoishi.a
	gcc -g -o koishi -Wall main.o -lkoishi -L./libkoishi

koishi.o: koishi.c
	gcc -g -o koishi.o -c -Wall -I./libkoishi koishi.c

libkoishi/libkoishi.a: libkoishi
	${MAKE} -C libkoishi

run: koishi
	./koishi

gdb: koishi
	gdb koishi

.PHONY: all run gdb