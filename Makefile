all: koishi

koishi: koishi.o libkoishi
	gcc -g -o koishi -Wall koishi.o -lkoishi -L./libkoishi

koishi.o: koishi.c
	gcc -g -o koishi.o -c -Wall -I./libkoishi koishi.c

libkoishi:
	${MAKE} -C libkoishi

run: koishi
	./koishi

gdb: koishi
	gdb koishi

.PHONY: all run gdb libkoishi