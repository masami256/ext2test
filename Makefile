CC = gcc

CFLAGS = -I. -Wall -g

target = ext2test

objs = ext2test.o

target:$(objs)
	$(CC) $(objs) -o $(target)

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -fr *.o *~ $(target)
