# -*- makefile -*-

AUTOMAKE_OPTIONS	= foreign dist-tarZ no-dependencies

SUBDIRS			= mpi2c++

INCLUDES		= $(OPTFLAGS) -I$(srcdir)/mpi2c++ -I${srcdir} $(MINI_BALKY_CXXFLAGS)
lib_LIBRARIES		= $(MPI2CPP_LIBRARY)
EXTRA_LIBRARIES		= libmpi++.a libpmpi++.a

libmpi___a_SOURCES	= \
			mpi++.cc \
			intercepts.cc
libpmpi___a_SOURCES	= \
			mpi++.cc \
                        intercepts.cc \
                        pmpi++.cc \
                        op.cc \
                        comm.cc \
                        intracomm.cc \
                        topology.cc \
                        intercomm.cc \
                        request.cc \
                        group.cc \
                        datatype.cc \
                        status.cc \
                        errhandler.cc \
                        exception.cc \
                        functions.cc

EXTRA_DIST		= $(libmpi___a_SOURCES) $(libpmpi___a_SOURCES)

SOURCEFILES		= $(EXTRA_DIST)
include $(top_srcdir)/Makefile.depend
clean-local:
	rm -f *.ii *.ti
