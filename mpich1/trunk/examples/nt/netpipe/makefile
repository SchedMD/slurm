CPP=gcc
FLAGS=-I../../../include -c
LINKFLAGS=-L../../../lib -lmpich
OBJS=GetOpt.o netmpi.o

netpipe : $(OBJS)
	$(CPP) -o netpipe.exe $(OBJS) $(LINKFLAGS)

GetOpt.o : GetOpt.cpp GetOpt.h
	$(CPP) $(FLAGS) GetOpt.cpp

netmpi.o : netmpi.cpp
	$(CPP) $(FLAGS) netmpi.cpp
