<<<<<<< HEAD
tinyServer : tinyServer.c tinyServer.h
	gcc -g -o tinyServer tinyServer.c

=======
CC = c99
CFLAGS = -Wall -O2

# LIB = -lpthread

all: tiny

tiny: tiny.c
	$(CC) $(CFLAGS) -o tiny tiny.c $(LIB)

clean:
	rm -f *.o tiny *~
>>>>>>> fae45b8d3bfd58322e1c242b52583aaef5da8c1c
