CC = gcc

CFLAGS = -I. -Wall -g

target = minixtest

objs = test.o

target:$(objs)
	$(CC) $(objs) -o $(target)

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -fr *.o *~ $(target) core
