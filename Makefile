CFLAGS=-Wall -Wextra -std=c11 -pedantic -ggdb -O3 -fno-strict-aliasing
# TODO: load Xext dynamic and if it's not available just don't use MIT-SHM, 'cause the app can work without it
LIBS=-lm -lX11 -lXext

metaballs: main.c
	$(CC) $(CFLAGS) -o metaballs main.c $(LIBS)
