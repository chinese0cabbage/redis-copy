DEBUG= -g
CFLAGS= -std=gnu11 -pedantic -O2 -Wall -W -DSDS_ABORT_ON_OOM -Wno-builtin-macro-redefined -U__file__ -D__FILE__='"$(notdir $<)"'

SERVER_OBJ = redis-server.o zmalloc.o sds.o util.o sha256.o fpconv_dtoa.o mt19937-64.c dict.c redisassert.c siphash.c adlist.c
CLIENT_OBJ = redis-client.o
BENCH_OBJ = redis-bench.o

all: redis-server redis-client redis-benchmark

redis-server.o: redis-server.c
redis-client.o: redis-client.c
redis-bench.o: redis-bench.c

redis-server: $(SERVER_OBJ) 
	$(CC) -o redis-server $(CFLAGS) $(SERVER_OBJ) -lm

redis-client: $(CLIENT_OBJ) 
	$(CC) -o redis-client $(CFLAGS) $(CLIENT_OBJ)
	
redis-benchmark: $(BENCH_OBJ) 
	$(CC) -o redis-benchmark $(CFLAGS) $(BENCH_OBJ)

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $(COMPILE_TIME) $<

dep:
	$(CC) -MM *.c

clean:
	rm -rf *.o redis-server redis-client redis-benchmark