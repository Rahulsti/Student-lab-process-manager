CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11

OBJS = main.o resource.o queue.o process.o logger.o tests.o

all: labmgr lab_session

labmgr: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

lab_session: lab_session.c
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ lab_session.c

%.o: %.c common.h
	$(CC) $(CFLAGS) -c $<

bench: bench.c resource.c queue.c process.c logger.c common.h lab_session
	$(CC) $(CFLAGS) -DMAXS=1024 -o bench bench.c resource.c queue.c process.c logger.c

test: all
	./labmgr --test

demo: all
	./labmgr -f scenarios/01_basic_lab.txt

clean:
	rm -f *.o labmgr lab_session bench labmgr.log bench_*.csv
