CFLAGS = -fpic -g -D_PTHREADS -I ~/work/gcc/git-master/gcc/

test-tramp: test-tramp.c libtramp.so
	$(CC) -g -o $@ -Wl,-rpath=. $^

libtramp.so: tramp-stack.o tramp-heap.o tramp-raw.o
	$(CC) $(CFLAGS) -o $@ -shared $^ -lpthread

clean:
	rm -f *.o *.so
