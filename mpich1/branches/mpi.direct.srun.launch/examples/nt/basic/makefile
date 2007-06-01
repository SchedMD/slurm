CC=gcc
CCFLAGS=-I../../../include -c
LINKFLAGS=-L../../../lib -lmpich
FC=g77
FCFLAGS=-I../../../include -c

cpi : cpi.o
	$(CC) -o cpi.exe cpi.o $(LINKFLAGS)

systest : systest.o
	$(CC) -o systest.exe systest.o $(LINKFLAGS)

fpi : fpi.o
	$(FC) -o fpi.exe fpi.o $(LINKFLAGS)

cpi.o : cpi.c
	$(CC) $(CCFLAGS) cpi.c

systest.o : systest.c
	$(CC) $(CCFLAGS) systest.c

fpi.o : fpi.f
	$(FC) $(FCFLAGS) fpi.f
