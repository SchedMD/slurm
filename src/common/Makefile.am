# Makefile for common library 

AUTOMAKE_OPTIONS = foreign

#
if DEBUG_MODULES
test_modules = bits_bytes bitstring
else
test_modules = 
endif

if WITH_ELAN
elan_sources = qsw.c
else
elan_sources = 
endif

noinst_LIBRARIES = 	libcommon.a

libcommon_a_SOURCES = 	xmalloc.c 	\
		      	xassert.c	\
		      	xstring.c	\
		      	strlcpy.c	\
			list.c  	\
		      	log.c		\
			safeopen.c	\
		      	bits_bytes.c	\
			bitstring.c     \
			pack.c		\
			$(elan_sources)

noinst_HEADERS = 	xmalloc.h 	\
		 	xassert.h 	\
		 	xstring.h	\
			macros.h	\
		 	list.h 		\
		 	log.h 		\
			safeopen.h	\
		 	strlcpy.h	\
		 	slurm.h 	\
		 	slurmlib.h	\
			bitstring.h     \
			bits_bytes.h    \
			pack.h		\
			qsw.h	

EXTRA_libcommon_a_SOURCES =		\
			qsw.c

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
