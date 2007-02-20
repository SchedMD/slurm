EXE_NAME=moab_acctper
CC = /usr/bin/gcc

# program executable *********
$(EXE_NAME): moab_acctper.o
#$(CC) -o $(EXE_NAME) moab_acctper.o
# .o files *****************

moab_acctper.o:	moab_acctper.c moab_acctper.h
	$(CC) -c moab_acctper.c	

clean:
	rm *.o

install:
	cp $(EXE_NAME) ../../bin/netmon
