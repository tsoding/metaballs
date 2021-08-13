CFLAGS=-Wall -Wextra -std=c11 -pedantic -ggdb -O3 -fno-strict-aliasing
LIBS=-lm -lX11

metaballs: main.c
	$(CC) $(CFLAGS) -o metaballs main.c $(LIBS)
