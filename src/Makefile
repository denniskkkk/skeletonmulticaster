LDFLAGS = -lpthread -levent
CFLAGS = -lpthread -levent

all: test

test: test.o
	$(CC) $(LDFLAGS) test.o -o test

test.o: test.c
	$(CC) $(CFLAGS) -c test.c

clean: 
	rm *.o test