# Makefile for common library 

#
if DEBUG_MODULES
test_modules = bits_bytes bitstring
else
test_modules = 
endif

if WITH_ELAN
elan_sources = qsw.c
elan_headers = qsw.h
else
elan_sources = 
elan_headers = 
endif

AUTOMAKE_OPTIONS = foreign

noinst_LIBRARIES = libcommon.a

libcommon_a_SOURCES = 	xmalloc.c 	\
		      	xassert.c	\
		      	xstring.c	\
		      	strlcpy.c	\
                      	list.c  	\
		      	log.c		\
		      	bits_bytes.c	\
			bitstring.c     \
			pack.c		\
			$(elan_sources)

noinst_HEADERS = 	xmalloc.h 	\
		 	xassert.h 	\
		 	xstring.h	\
		 	list.h 		\
		 	log.h 		\
		 	strlcpy.h	\
		 	slurm.h 	\
		 	slurmlib.h	\
			bitstring.h     \
			pack.h		\
			$(elan_headers)

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#bits_bytes_CFLAGS     = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
bits_bytes     : bits_bytes_d.o log.o xmalloc.o xstring.o xassert.o strlcpy.o 
% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
