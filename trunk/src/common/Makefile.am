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

noinst_LTLIBRARIES = 	libcommon.la

libcommon_la_SOURCES = 	xmalloc.c 	\
		      	xassert.c	\
		      	xstring.c	\
		      	strlcpy.c	\
			list.c  	\
		      	log.c		\
			safeopen.c	\
			bitstring.c     \
			pack.c		\
			xerrno.c	\
			nodelist.c	\
			slurm_protocol_api.c	\
			slurm_protocol_pack.c	\
			slurm_protocol_util.c	\
			slurm_protocol_socket_implementation.c	\
			slurm_protocol_defs.c	\
			nodelist.c\
			$(elan_sources)

noinst_HEADERS = 	xmalloc.h 	\
		 	xassert.h 	\
		 	xstring.h	\
			macros.h	\
		 	list.h 		\
		 	log.h 		\
			safeopen.h	\
		 	strlcpy.h	\
			bitstring.h     \
			pack.h		\
			xerrno.h	\
			slurm_protocol_api.h	\
			slurm_protocol_pack.h	\
			slurm_protocol_util.h	\
			slurm_protocol_defs.h	\
			slurm_protocol_common.h	\
			slurm_protocol_socket_common.h	\
			slurm_protocol_mongo_common.h	\
			nodelist.h\
			qsw.h	

EXTRA_libcommon_la_SOURCES =		\
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
