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
			hostlist.c	\
			parse_spec.c	\
			slurm_errno.c	\
			slurm_protocol_api.c	\
			slurm_protocol_pack.c	\
			slurm_protocol_util.c	\
			slurm_protocol_socket_implementation.c	\
			slurm_protocol_defs.c	\
			util_signals.c \
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
			hostlist.h	\
			parse_spec.h	\
			slurm_protocol_api.h	\
			slurm_protocol_pack.h	\
			slurm_protocol_util.h	\
			slurm_protocol_defs.h	\
			slurm_protocol_common.h	\
			slurm_protocol_socket_common.h	\
			slurm_protocol_mongo_common.h	\
			slurm_protocol_interface.h	\
			slurm_errno.h		\
			qsw.h			\
			util_signals.h

EXTRA_libcommon_la_SOURCES =		\
			qsw.c
