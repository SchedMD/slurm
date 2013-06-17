CFLAGS=-Wall -Wextra -g

all: libpmi2.so testpmi2 testpmi2_put

testpmi2: LDLIBS+=-L. -lpmi2
testpmi2: CFLAGS+=-I.

testpmi2_put: LDLIBS+=-L. -lpmi2
testpmi2_put: CFLAGS+=-I.


%.o: CFLAGS=-fPIC

libpmi2.so: LDFLAGS+= -shared -Wl,-soname,libpmi2.so
libpmi2.so: pmi2_api.o pmi2_util.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)


clean:
	rm -f *.so *.o testpmi2 testpmi2_put

